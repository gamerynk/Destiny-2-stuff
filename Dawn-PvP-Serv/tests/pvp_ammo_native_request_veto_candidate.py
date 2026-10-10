import os
"""Private composition of native ownership request and attachment-veto leaves.

No process access, live writes, native calls in a game, or function stubs.
The synthetic pool deliberately distinguishes intent from established attachment.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

UPSTREAM = Path(os.environ['DAWN_NATIVE_FIXTURES']) / 'ui'
fixture = UPSTREAM / 'replay_ammo_attachment_veto.py'
namespace = {'__file__': str(fixture)}
exec(compile(fixture.read_text().split('results=[]')[0], str(fixture), 'exec'), namespace)
globals().update({key: value for key, value in namespace.items() if key != '__file__'})
u.mem_map(0x30B0000, 0x20000)
rep = 0x4070000
pool = 0x30B0440
token = 0x12345


def invoke(address, rcx, rdx=0):
    u.reg_write(UC_X86_REG_RCX, rcx)
    u.reg_write(UC_X86_REG_RDX, rdx)
    u.reg_write(UC_X86_REG_RSP, stack)
    w64(stack, 0x100000)
    u.emu_start(address, 0x100000, count=2000)
    assert u.reg_read(UC_X86_REG_RIP) == 0x100000
    return u.reg_read(UC_X86_REG_RAX) & 255


cases = []
for state in ('unattached_intent', 'stale_attachment', 'established_reciprocal_attachment'):
    u.mem_write(rep, bytes(0x10000))
    u.mem_write(pool, bytes(0x70))
    u.mem_write(rep + 0x114 + 6 * (token & 0x1FFF), struct.pack('<h', 0))
    w32(rep + 0xC520 + 4 * ((token & 0x1FFF) >> 5), 1 << (token & 31))
    w32(pool + 8, token)
    u.mem_write(component, bytes(0x300))
    u.mem_write(unit, bytes(0x900))
    w32(component + 0x290, 0x1234)
    w32(component + 0x294, -1 if state == 'unattached_intent' else 1)
    w32(unit + 0x24, 0x5678 if state == 'stale_attachment' else 0x1234)
    w32(unit + 0x8A0, 2)
    w32(unit + 0x2C, 1)
    before_component = bytes(u.mem_read(component, 0x300))
    before_unit = bytes(u.mem_read(unit, 0x900))
    before_row = bytes(u.mem_read(pool, 0x70))
    veto_before = invoke(0xDE7B50, component)
    invoke(0x171AE40, rep, token)
    requested = struct.unpack('<I', u.mem_read(rep + 0xCFA4, 4))[0]
    assert requested == 1
    veto_after = invoke(0xDE7B50, component)
    assert veto_after == veto_before == int(state == 'established_reciprocal_attachment')
    assert bytes(u.mem_read(component, 0x300)) == before_component
    assert bytes(u.mem_read(unit, 0x900)) == before_unit
    assert bytes(u.mem_read(pool, 0x70)) == before_row
    cases.append({'state': state, 'native_request_enqueued': True,
                  'native_attachment_veto_before': bool(veto_before),
                  'native_attachment_veto_after': bool(veto_after),
                  'association_and_row_unchanged': True})

result = {'scope': 'original171AE40 request producer followed by completeDE7B50 reciprocal predicate/352310/BC3390; controlled generation-valid pools; no helper stubs',
          'text_sha256': hashlib.sha256(raw).hexdigest(),
          'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
          'cases': cases,
          'conclusion': 'Native request enqueue is independent of established reciprocal attachment. Existing attachment veto cannot stand in for the pre-attachment proximity lease merely because request has been enqueued.',
          'limits': ['No complete1725D80 classification or170CAA0 group replay.',
                     'No native grant, attachment publication, grant-to-collect ordering, authority handoff, or geometry execution.',
                     'No claim that this synthetic unattached object is a live player pickup; actual component/request association must be captured before removal.',
                     'Never establish fake reciprocal attachment as an ownership-retention producer.'],
          'pass': True}
if len(sys.argv) != 2:
    raise SystemExit('usage: pvp_ammo_native_request_veto_candidate.py NEW_OUTPUT_JSON')
with Path(sys.argv[1]).open('x', encoding='utf-8') as destination:
    json.dump(result, destination, indent=2)
print(f'PASS {len(cases)} composed native request/attachment cases')
