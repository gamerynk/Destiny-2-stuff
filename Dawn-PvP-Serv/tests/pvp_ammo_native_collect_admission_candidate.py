import os
"""Actual native override-quantity and collection front-gate experiment.

The amount computation does not establish authority. This is not a replay of
the predicate that admits the separate collection owner or of ammo grants.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
from unicorn import *
from unicorn.x86_const import *

archive = Path(os.environ['DAWN_NATIVE_TEXT'])
raw = archive.read_bytes()
u = Uc(UC_ARCH_X86, UC_MODE_64)
u.mem_map(0x1000, (len(raw) + 4095) // 4096 * 4096)
u.mem_write(0x1000, raw)
for address, size in ((0x1F90000, 0x8000), (0x26BE000, 0x1000), (0x4000000, 0x10000)):
    u.mem_map(address, size)
component, ammo_identity, units, stack, stop = 0x4001000, 0x4002000, 0x4003000, 0x400F008, 0x100000


def w32(address, value):
    u.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))


def call(address, lane):
    u.mem_write(stack, struct.pack('<Q', stop))
    for reg, value in ((UC_X86_REG_RSP, stack), (UC_X86_REG_RCX, component),
                       (UC_X86_REG_RDX, lane), (UC_X86_REG_R8, ammo_identity)):
        u.reg_write(reg, value)
    u.emu_start(address, stop, count=10000)
    assert u.reg_read(UC_X86_REG_RIP) == stop


u.mem_write(component + 0x2D0, b'\x02')
u.mem_write(component + 0x2DC, b'\x01\x02')
w32(component + 0x2D4, 0x811C9DC5)
u.mem_write(component + 0x2D8, struct.pack('<f', 3.5))
w32(component + 0x2C, 123)
w32(ammo_identity, 0x811C9DC5)
u.mem_write(0x1F90E18, struct.pack('<Q', units))
w32(0x1F90E20, 0xE0)
cases = []
visited = []
u.hook_add(UC_HOOK_CODE, lambda uc, address, size, user: visited.append(address))
for authority in (False, True):
    w32(0x26BE0EC, (1 << (123 & 31)) if authority else 0)
    for lane in (0, 1, 2):
        call(0xE8BBC0, lane)
        amount = struct.unpack('<f', struct.pack('<I', u.reg_read(UC_X86_REG_XMM0) & 0xFFFFFFFF))[0]
        assert amount == (3.5 if lane == 1 else 0.0)
        before = bytes(u.mem_read(component, 0x400))
        visited.clear()
        call(0xE94C30, 1)
        assert (0xE94C87 in visited) == authority
        assert bytes(u.mem_read(component, 0x400)) == before
        cases.append({'authority': authority, 'requested_ammo_lane': lane,
                      'native_override_quantity': amount,
                      'collection_passed_authority_gate': authority,
                      'inactive_player_prevented_grant': True})

result = {'scope': 'complete native override branchE8BBC0 and originalE94C30 authority/player front gates; private controlled component/player rows; code hook observes instructions only, no function stubs',
          'text_sha256': hashlib.sha256(raw).hexdigest(), 'cases': cases,
          'conclusion': 'A positive supported special override quantity is independent of local authority. The separate collection-owner quantity test alone cannot replace its unresolved provider predicate/authority ordering.',
          'limits': ['No full collection owner28CFA0→12EC6DD provider predicate replay.',
                     'No claim that a missing-authority collection call is admitted by a supported live caller.',
                     'No grant, reserves, consumption, replication feedback or cleanup executed.'],
          'pass': True}
if len(sys.argv) != 2:
    raise SystemExit('usage: pvp_ammo_native_collect_admission_candidate.py NEW_OUTPUT_JSON')
with Path(sys.argv[1]).open('x', encoding='utf-8') as destination:
    json.dump(result, destination, indent=2)
print(f'PASS {len(cases)} native quantity/collection-front cases')
