from __future__ import annotations

import ctypes
from functools import lru_cache
import json
from pathlib import Path
import re
import struct

from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def u32(data, offset=0):
    return struct.unpack_from('<I', data, offset)[0]


def array(data, offset, expected_class=None, stride=None):
    if offset < 0 or offset + 16 > len(data):
        raise ValueError('Array descriptor out of bounds')
    count, relative = struct.unpack_from('<Qq', data, offset)
    if count == 0:
        return 0, 0, expected_class
    header = offset + 8 + relative
    if count > 300000 or header < 4 or header + 16 > len(data):
        raise ValueError('Array header out of bounds')
    repeated, cls = struct.unpack_from('<QI', data, header)
    if repeated != count or (expected_class is not None and cls != expected_class):
        raise ValueError('Array class or count mismatch')
    start = header + 16
    if stride and start + count * stride > len(data):
        raise ValueError('Array payload out of bounds')
    return count, start, cls


class Packages:
    def __init__(self, game_root: Path, image: Path):
        self.root = game_root
        self.paths = {}
        for p in (game_root / 'packages').glob('*.pkg'):
            match = re.fullmatch(r'(.+)_([0-9a-fA-F]{4})_(\d+)', p.stem)
            if match:
                pid, patch = int(match[2], 16), int(match[3])
                if pid not in self.paths or patch > self.paths[pid][0]:
                    self.paths[pid] = (patch, p)
        if not self.paths:
            raise ValueError('No package families found')
        if isinstance(image, int):
            from mapped_image import read_process_image
            mapped = read_process_image(image, game_root)
        elif image.suffix == '.json':
            info = json.loads(image.read_text())
            ranges = info['ranges']
            mapped = bytearray(max(r['rva'] + r['size'] for r in ranges))
            for r in ranges:
                data = (image.parent / ('native-' + r['name'] + '.bin')).read_bytes()
                if len(data) != r['size']:
                    raise ValueError('Captured section size mismatch')
                mapped[r['rva']:r['rva'] + r['size']] = data
        else:
            mapped = image.read_bytes()
        table_matches = list(re.finditer(rb'\x0f\x10\x05....\x48\x8d\x64\x24\xf8\x48\x89\x2c\x24\x48\x8d\x2d....\xe9', mapped, re.S))
        token_matches = list(re.finditer(rb'\x0f\x10\x44\x24.\x41\xb8\x14\x00\x00\x00\x48\x8d\x15....\x0f\x10\x4c\x24.\x48\x8d\x8c\x24....', mapped, re.S))
        if len(table_matches) != 1 or len(token_matches) != 1:
            raise ValueError('Mapped image does not match the supported content reader')
        p, t = table_matches[0].start(), token_matches[0].start()
        table = p + 7 + struct.unpack_from('<i', mapped, p+3)[0]
        token = t + 18 + struct.unpack_from('<i', mapped, t+14)[0]
        if min(table, token) < 0 or table + 44 > len(mapped) or token + 16 > len(mapped):
            raise ValueError('Mapped content material is out of bounds')
        self._primary = bytes((mapped[token+i]+mapped[table+16+i]) & 255 for i in range(16))
        self._alternate = bytes(mapped[table:table+16])
        self._nonce = bytes(mapped[table+32:table+44])
        del mapped
        self._codec = ctypes.CDLL(str(game_root/'bin/x64/oo2core_3_win64.dll'))
        self._decode = self._codec.OodleLZ_Decompress
        self._decode.restype = ctypes.c_int64
        self._decode.argtypes = [ctypes.c_void_p,ctypes.c_int64,ctypes.c_void_p,ctypes.c_int64,ctypes.c_int,ctypes.c_int,ctypes.c_int64,ctypes.c_void_p,ctypes.c_int64,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int64,ctypes.c_int]

    @lru_cache(maxsize=64)
    def table(self, pid):
        path = self.paths[pid][1]
        with path.open('rb') as f:
            header = f.read(0x180)
            if struct.unpack_from('<H', header)[0] != 38:
                raise ValueError('Unsupported package version')
            count, blocks = u32(header, 0xb4), u32(header, 0xd0)
            if count > 8192 or blocks > 8192:
                raise ValueError('Package table exceeds supported limits')
            f.seek(u32(header, 0x110)+96)
            entries = f.read(count*16)
            f.seek(32, 1)
            block_table = f.read(blocks*48)
        if len(entries) != count*16 or len(block_table) != blocks*48:
            raise ValueError('Truncated package table')
        return path, entries, block_table

    def tags(self, pid, cls):
        entries = self.table(pid)[1]
        return [0x80800000+(pid<<13)+i for i in range(len(entries)//16) if u32(entries,i*16)==cls]

    @lru_cache(maxsize=32)
    def block(self, pid, index):
        path, _, blocks = self.table(pid)
        offset, size, patch, flags = struct.unpack_from('<IIHH', blocks, index*48)
        source = path.with_name(path.stem.rsplit('_',1)[0] + f'_{patch}.pkg')
        with source.open('rb') as f:
            f.seek(offset)
            raw = f.read(size)
        if len(raw) != size:
            raise ValueError('Truncated package block')
        if flags & 2:
            nonce = bytearray(self._nonce)
            nonce[0] ^= pid >> 8
            nonce[1] = 0xf9
            nonce[11] ^= pid & 255
            raw = AESGCM(self._alternate if flags&4 else self._primary).decrypt(bytes(nonce),raw+blocks[index*48+32:index*48+48],None)
        if flags & 1:
            out = ctypes.create_string_buffer(0x40000+64)
            source = ctypes.create_string_buffer(raw)
            for wanted in range(0x40000,0,-0x4000):
                got=self._decode(source,len(raw),out,wanted,0,0,0,None,0,None,None,None,0,3)
                if got==wanted:
                    return out.raw[:wanted]
            raise ValueError('Package decompression failed')
        return raw

    @lru_cache(maxsize=256)
    def read(self, tag):
        if not 0x80800000 <= tag <= 0xffffffff:
            raise ValueError('Invalid tag handle')
        pid,index=(tag-0x80800000)>>13,tag&0x1fff
        _,entries,_=self.table(pid)
        cls,_,placement=struct.unpack_from('<IIQ',entries,index*16)
        block,offset,size=placement&0x3fff,((placement>>14)&0x3fff)*16,placement>>28
        if size>64*1024*1024:
            raise ValueError('Tag exceeds extraction bound')
        data=bytearray()
        while len(data)<size:
            chunk=self.block(pid,block)
            if offset>=len(chunk):raise ValueError('Entry offset outside decoded block')
            data.extend(chunk[offset:offset+size-len(data)])
            block+=1
            offset=0
        return cls,bytes(data)

    def names(self):
        result={}
        for pid,(patch,path) in sorted(self.paths.items()):
            with path.open('rb') as f:
                header=f.read(244)
                misc=u32(header,240)
                if not misc:continue
                variant=header[26]
                if variant not in (0,1):raise ValueError('Unsupported name table variant')
                descriptor=misc+(8 if variant==0 else 16)
                f.seek(descriptor)
                count,rel,_=struct.unpack('<QQQ',f.read(24))
                if count>8192:raise ValueError('Name table exceeds capacity')
                table=descriptor+24+rel
                for i in range(count):
                    at=table+i*16
                    f.seek(at)
                    tag,cls,name_rel=struct.unpack('<IIQ',f.read(16))
                    f.seek(at+8+name_rel)
                    raw=f.read(128)
                    end=raw.find(b'\0')
                    if end<=0:raise ValueError('Invalid name string')
                    name=raw[:end].decode('utf-8')
                    row={'name':name,'tag':tag,'class':cls,'package':path.name,'package_id':pid,'patch':patch}
                    if name not in result or patch>result[name]['patch']:
                        result[name]=row
        return result
