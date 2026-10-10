import struct
from functools import lru_cache

from package_reader import array, u32


class Strings:
    def __init__(self, packages):
        self.packages = packages

    @lru_cache(maxsize=128)
    def bank(self, tag):
        cls, blob = self.packages.read(tag)
        if cls != 0x80809A88:
            raise ValueError('Expected string bank')
        count, start, _ = array(blob, 8, 0x80800070, 4)
        cls, english = self.packages.read(u32(blob, 24))
        if cls != 0x80809A8A:
            raise ValueError('Expected English string data')
        part_count, parts, _ = array(english, 8, 0x80809A90, 32)
        row_count, rows, _ = array(english, 72, 0x80809A8E, 16)
        if count != row_count:
            raise ValueError('String key and value count mismatch')
        result = {}
        for i in range(count):
            key = u32(blob, start + i * 4)
            row = rows + i * 16
            relative, count_for_string = struct.unpack_from('<qQ', english, row)
            first = row + relative
            if count_for_string and (first < parts or (first - parts) % 32
                    or first + count_for_string * 32 > parts + part_count * 32):
                raise ValueError('String part reference out of bounds')
            chunks = []
            for j in range(count_for_string):
                part = first + j * 32
                at = part + 8 + struct.unpack_from('<q', english, part + 8)[0]
                size = struct.unpack_from('<H', english, part + 20)[0]
                bias = struct.unpack_from('<H', english, part + 24)[0]
                if at < 0 or at + size > len(english):
                    raise ValueError('String payload out of bounds')
                text = english[at:at + size].decode('utf-8')
                chunks.append(''.join(chr(ord(c) + bias) for c in text))
            result[key] = ''.join(chunks)
        return result
