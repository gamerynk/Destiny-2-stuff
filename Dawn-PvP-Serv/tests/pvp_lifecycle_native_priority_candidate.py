import os
"""Private native priority authoring experiment; no process or production writes.

Execute the original gather and candidate sort with synthetic resource bindings.
This tests a potential authored-data replacement, not live entity binding proof.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

UPSTREAM = Path(os.environ['DAWN_NATIVE_FIXTURES']) / 'replication'
fixture = UPSTREAM / 'replay_native_entity_gather.py'
namespace = {'__file__': str(fixture)}
exec(compile(fixture.read_text().split('try:')[0], str(fixture), 'exec'), namespace)
globals().update({key: value for key, value in namespace.items() if key != '__file__'})


def gather(flags, age, parameters, bound=True):
    write(ROWS, bytes(0x70))
    write(ROWS + 1, bytes([0xFD if flags == 0x88 else 0xFF]))
    for offset in (0xC, 0x44, 0x48):
        write(ROWS + offset, struct.pack('<I', 0xFFFFFFFF))
    write(ROWS + 0x60, struct.pack('<Q', AUX))
    write(AUX, bytes(0x80))
    write(AUX + 4, struct.pack('<I', 1))
    write(AUX + 8, struct.pack('<I', 0 if bound else 0xFFFFFFFF))
    write(AUX + 0x38, struct.pack('<Q', CLOCK_NOW - age))
    write(AUX + 0x50, struct.pack('<H', flags))
    write(RESOURCE + 0x118, bytes(parameters))
    write(OUTPUT, bytes(4))
    before = bytes(u.mem_read(ROWS, 0x70))
    sp = STACK + 0x800
    write(sp, struct.pack('<Q', STOP))
    write(sp + 0x28, struct.pack('<Q', 1))
    write(sp + 0x30, struct.pack('<Q', OUTPUT))
    for reg, value in ((UC_X86_REG_RSP, sp), (UC_X86_REG_RCX, LANE),
                       (UC_X86_REG_RDX, 1), (UC_X86_REG_R8, 2), (UC_X86_REG_R9, 0)):
        u.reg_write(reg, value)
    u.emu_start(BASE + 0x170C080, STOP, count=200000)
    assert u.reg_read(UC_X86_REG_RIP) == STOP
    assert u.reg_read(UC_X86_REG_RAX) == 1
    assert bytes(u.mem_read(ROWS, 0x70)) == before
    return struct.unpack('<I', u.mem_read(OUTPUT, 4))[0]


def sort(words):
    address = OUTPUT + 0x1000
    write(address, struct.pack('<' + 'I' * len(words), *words))
    sp = STACK + 0x800
    write(sp, struct.pack('<Q', STOP))
    for reg, value in ((UC_X86_REG_RSP, sp), (UC_X86_REG_RCX, address), (UC_X86_REG_RDX, len(words))):
        u.reg_write(reg, value)
    u.emu_start(BASE + 0x1767A30, STOP, count=200000)
    assert u.reg_read(UC_X86_REG_RIP) == STOP
    return list(struct.unpack('<' + 'I' * len(words), u.mem_read(address, 4 * len(words))))


original = (UPSTREAM / 'asset-80c70edf.bin').read_bytes()[0x200:0x22C]
fixed = bytearray(original)
struct.pack_into('<f', fixed, 0, 1.0)
aged = bytearray(original)
struct.pack_into('<ii', aged, 0xC, 250, 250)
struct.pack_into('<ff', aged, 0x1C, 1.0, 1.0)
struct.pack_into('<i', aged, 0x24, 250)
struct.pack_into('<f', aged, 0x28, 1.0)
cases = []
for name, parameters in [('original', original), ('fixed_one', fixed), ('aged_one', aged)]:
    for work, flags in [('creation', 0x81), ('handoff', 0x88), ('ordinary_movement', 0x82)]:
        for age in (20, 250, 1000):
            word = gather(flags, age, parameters)
            priority = word >> 15 & 1023
            if name == 'fixed_one':
                assert priority == 1023
            if name == 'aged_one' and age >= 250:
                assert priority == 1023
            cases.append({'authoring': name, 'work': work, 'age_ms': age,
                          'priority': priority, 'root_rows_unchanged': True})

fallback = []
for age in (20, 250, 1000):
    values = [gather(0x81, age, parameters, False) for parameters in (original, fixed, aged)]
    assert len(set(values)) == 1
    fallback.append({'age_ms': age, 'priority': values[0] >> 15 & 1023,
                     'resource_authoring_has_no_effect': True})

ties = []
lifecycle = gather(0x81, 250, fixed)
for count in (8, 64):
    combat = [(1023 << 15) | token for token in range(1, count + 1)]
    for position in ('first', 'last'):
        words = ([lifecycle] + combat) if position == 'first' else (combat + [lifecycle])
        ordered = sort(words)
        rank = ordered.index(lifecycle)
        ties.append({'combat_candidates': count, 'input_position': position,
                     'native_sorted_rank': rank, 'admitted_with_four_records': rank < 4})
assert any(not row['admitted_with_four_records'] for row in ties)

result = {'scope': 'original170C080 gather plus1767A30 sort; private synthetic rows and resource bindings; no function stubs',
          'text_sha256': hashlib.sha256((ASSETS / 'native-text.bin').read_bytes()).hexdigest(),
          'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
          'candidate_parameter_bytes': {'fixed_one': fixed.hex(), 'aged_one': aged.hex()},
          'cases': cases, 'unresourced_creation_fallback': fallback, 'combat_ties': ties,
          'conclusion': 'Authored priority can promote bound roots but also promotes ordinary movement; unresourced roots remain unaffected and tied maximum combat can precede lifecycle. This does not replace the bounded fairness hook.',
          'limitations': ['No actual current pickup/player/root resource binding closure.',
                          'No packet capacity/acknowledgement replay or peer gameplay.',
                          'Removal resource path requires genuine43D4A0 metadata and is deliberately not stubbed.',
                          'Child creation uses root last-send age; authored root priority cannot distinguish child wait from ordinary movement.'],
          'pass': True}
if len(sys.argv) != 2:
    raise SystemExit('usage: pvp_lifecycle_native_priority_candidate.py NEW_OUTPUT_JSON')
with Path(sys.argv[1]).open('x', encoding='utf-8') as destination:
    json.dump(result, destination, indent=2)
print(f'PASS {len(cases)} native priority cases, {len(fallback)} fallbacks, {len(ties)} combat sorts')
