import os
"""Original185366C..18536C4 component-argument construction, privately.

Starts AFTER native method lookup. The callback slot is supplied, so this is
not proof of A40F40 callback selection or a full dispatcher/callback replay.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
from unicorn import *
from unicorn.x86_const import *

text_path = Path(os.environ['DAWN_NATIVE_TEXT'])
text = text_path.read_bytes()
u = Uc(UC_ARCH_X86, UC_MODE_64)
u.mem_map(0x1000, (len(text) + 4095) // 4096 * 4096)
u.mem_write(0x1000, text)
u.mem_map(0x2439000, 0x1000)
u.mem_map(0x4000000, 0x200000)
header, tables, records, descriptor, stack = 0x4000000, 0x4010000, 0x4110000, 0x4120000, 0x41FF000


def w32(address, value):
    u.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))


def w64(address, value):
    u.mem_write(address, struct.pack('<Q', value))


w64(0x2439C70, header)
w64(header, tables)
cases = []
for datum in (1, 0x2001, 0x3E001, 0xFFFF0001):
    shifted = ((datum if datum < 0x80000000 else datum - 0x100000000) >> 13) & 0xFFFFFFFF
    bank = ((shifted | 0x0FFC0000) >> 18) & (shifted & 0xFFFF)
    table = tables + bank * 0x40
    w64(table + 8, records)
    w32(table + 0x30, 0x100)
    for mask in (0, 0x1000, 0xFFFFFFFF):
        w32(table + 0x34, mask)
        for offset in (0x30, 0x2030):
            relocation = 0x1000
            w64(records + 0x108, relocation)
            w32(descriptor + 8, datum)
            w64(descriptor + 0x10, offset)
            w64(stack + 0x28, 0xE8A870)
            for reg, value in ((UC_X86_REG_RDI, descriptor), (UC_X86_REG_RSI, 0x4121000),
                               (UC_X86_REG_RBP, 0x4122000), (UC_X86_REG_RBX, 42),
                               (UC_X86_REG_RSP, stack)):
                u.reg_write(reg, value)
            u.emu_start(0x185366C, 0x18536C4, count=100)
            assert u.reg_read(UC_X86_REG_RIP) == 0x18536C4
            signed_mask = mask if mask < 0x80000000 else mask - 0x100000000
            expected = records + (datum & 0x1FFF) * 0x100 + offset - (relocation & (signed_mask & 0xFFFFFFFFFFFFFFFF))
            actual = u.reg_read(UC_X86_REG_RCX)
            assert actual == expected
            assert u.reg_read(UC_X86_REG_RDX) == 42
            assert u.reg_read(UC_X86_REG_R8) == 0x4122000
            assert u.reg_read(UC_X86_REG_R9) == 0x4121000
            cases.append({'datum': hex(datum), 'bank': bank, 'mask': hex(mask),
                          'offset': hex(offset), 'callback_component': hex(actual),
                          'all_four_callback_arguments_match': True})
result = {'scope': 'unchanged native185366C..18536C4 argument construction only, after method resolution; private synthetic typed banks; no helper stubs, calls or writes to game',
          'text_sha256': hashlib.sha256(text).hexdigest(),
          'entry_bytes': text[0x185366C - 0x1000:0x18536C4 - 0x1000].hex(),
          'cases': cases, 'pass': True,
          'unproved': ['Actual A40F40 callback selection from a real F32CD0 descriptor.',
                       'F32CD0 caller/component descriptor ownership across original query.',
                       'Full native outer query, geometry, eligibility and E8A870 execution.'],
          'conclusion': 'The retained outer query carries the same typed reference that native dispatch uses to construct E8A870 component. Static/differential identity math is proved; callback selection and lifetime still gate omission.'}
if len(sys.argv) != 2:
    raise SystemExit('usage: pvp_native_query_dispatch_identity_replay.py NEW_OUTPUT_JSON')
with Path(sys.argv[1]).open('x', encoding='utf-8') as destination:
    json.dump(result, destination, indent=2)
print(f'PASS {len(cases)} original native dispatcher argument cases; method lookup excluded explicitly')
