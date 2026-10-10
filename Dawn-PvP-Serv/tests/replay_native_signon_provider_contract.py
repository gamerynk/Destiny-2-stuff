from pathlib import Path
import hashlib
import json
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RDI, UC_X86_REG_RIP, UC_X86_REG_RSP

assets = Path(sys.argv[1])
text = (assets / 'native-text.bin').read_bytes()
report = {'nativeTextSha256': hashlib.sha256(text).hexdigest(), 'targetMutations': 0,
          'bdNetCases': [], 'steamCases': [],
          'limits': ['Steam VM pointer decoding and auth-mode implementation are not emulated.',
                     'Backing states are synthetic; this validates predicates, not startup ordering.']}
u = Uc(UC_ARCH_X86, UC_MODE_64)
u.mem_map(0x1000, 0x4000000)
u.mem_write(0x1000, text)
stack, stop, state, auth = 0x3800000, 0x3700000, 0x3900000, 0x3920000
def put(address, value, fmt='<Q'):
    u.mem_write(address, struct.pack(fmt, value))
put(0x35B3DC0, state)
def run(rva, end=stop):
    put(stack, stop)
    u.reg_write(UC_X86_REG_RSP, stack)
    u.emu_start(rva, end, count=10000)
    assert u.reg_read(UC_X86_REG_RIP) == end, 'Predicate failed to reach its return boundary'
    return u.reg_read(UC_X86_REG_RAX) & 0xFF

for value in [-0x80000000, -4, -3, -2, -1, 0, 1, 2, 3, 0x7FFFFFFF]:
    put(state+0x298, value, '<i')
    ready = run(0x9E4770)
    failed = run(0x3F2BA0)
    assert ready == (value == 2)
    assert failed == (((value-1) & 0xFFFFFFFF) > 1)
    report['bdNetCases'].append({'status': value, 'ready': bool(ready), 'failed': bool(failed)})

for failed in [0, 1]:
    u.mem_write(0x26B45FB, bytes([failed]))
    assert run(0x3DF110) == failed

auth_mode = [0]
def substitute_mode(uc, address, size, user):
    if address == 0x4483B0:
        sp = uc.reg_read(UC_X86_REG_RSP)
        return_address = struct.unpack('<Q', uc.mem_read(sp, 8))[0]
        uc.reg_write(UC_X86_REG_RAX, auth_mode[0])
        uc.reg_write(UC_X86_REG_RSP, sp+8)
        uc.reg_write(UC_X86_REG_RIP, return_address)
mode_hook = u.hook_add(UC_HOOK_CODE, substitute_mode)
for value in [-1, 0, 1, 2, 3, 4, 5, 0x7FFFFFFF]:
    put(state+0x12DC0, value, '<i')
    for fields in range(16):
        for offset, bit in zip([4, 8, 12, 16], range(4)):
            put(auth+offset, 1 if fields & (1 << bit) else 0, '<I')
        for mode in [0, 1]:
            auth_mode[0] = mode
            u.reg_write(UC_X86_REG_RBX, state)
            u.reg_write(UC_X86_REG_RDI, auth)
            ready = run(0x3DF7B1, 0x3DF7E8)
            expected = value >= 1 and (value < 4 or
                       (fields & 12 == 12 and (not mode or fields & 3 == 3)))
            assert ready == expected, (value, fields, mode, ready)
            report['steamCases'].append({'state': value, 'initializedFields': fields,
                                         'authMode': mode, 'ready': bool(ready)})
u.hook_del(mode_hook)

for rva in [0x4053A0, 0x405450]:
    assert bytes(u.mem_read(rva, 1)) == b'\xE9'
    u.mem_write(rva, bytes.fromhex('48895c2408'))
report['aggregatePrologRecovery'] = {'rvas': ['0x4053A0', '0x405450'],
    'bytes': '48895c2408', 'basis': 'Original signature and Ghidra function prolog',
    'modifiedBytesPerFunction': 5}
provider_a, provider_b, vtable_a, vtable_b = 0x3960000, 0x3961000, 0x3962000, 0x3963000
put(provider_a, vtable_a)
put(provider_b, vtable_b)
put(0x26BE2E0, provider_a)
put(0x26BE2E8, provider_b)
for table, offset in [(vtable_a, 0), (vtable_b, 0x10)]:
    put(table+0x58, 0x3600000+offset)
    put(table+0x60, 0x3600020+offset)
    put(table+0x38, 0x3600040+offset)
    put(table+0x40, 0x3600060+offset)
    put(table+0x48, 0x3600080+offset)
fixture = {'ready': [False, False], 'failed': [False, False], 'requestCalls': 0,
           'errors': [], 'ticks': [], 'signonResultCalls': 0,
           'initResults': [True, True], 'initCalls': [], 'resetCalls': []}
def native_return(uc, value=0):
    sp = uc.reg_read(UC_X86_REG_RSP)
    target = struct.unpack('<Q', uc.mem_read(sp, 8))[0]
    uc.reg_write(UC_X86_REG_RAX, value)
    uc.reg_write(UC_X86_REG_RSP, sp+8)
    uc.reg_write(UC_X86_REG_RIP, target)
def provider_leaf(uc, address, size, user):
    if address in [0x3600000, 0x3600010]:
        native_return(uc, int(fixture['ready'][(address-0x3600000)//16]))
    elif address in [0x3600020, 0x3600030]:
        native_return(uc, int(fixture['failed'][(address-0x3600020)//16]))
    elif address in [0x3600040, 0x3600050]:
        fixture['ticks'].append((address-0x3600040)//16)
        native_return(uc)
    elif address in [0x3600060, 0x3600070]:
        index = (address-0x3600060)//16
        fixture['initCalls'].append(index)
        native_return(uc, int(fixture['initResults'][index]))
    elif address in [0x3600080, 0x3600090]:
        fixture['resetCalls'].append((address-0x3600080)//16)
        native_return(uc)
    elif address in [0x46AA00, 0xC25A30, 0x1071500, 0x187C480, 0xE2E960, 0xC25FE0, 0xC25F50]:
        native_return(uc)
    elif address in [0x3EB3A0, 0x3E9FF0, 0x3EBEC0, 0x3EBE10, 0x3E8980, 0x3EBE60, 0x3E9F50, 0x38FFA0]:
        native_return(uc)
    elif address == 0x3F83D0:
        native_return(uc, 1)
    elif address == 0x46AED0:
        native_return(uc, 1)
    elif address == 0x46A6C0:
        dest = uc.reg_read(UC_X86_REG_RCX)
        put(dest, 0, '<I')
        native_return(uc, dest)
    elif address == 0x1071620:
        fixture['signonResultCalls'] += 1
        native_return(uc, int(fixture['signonResultCalls'] > 1))
    elif address == 0x46B170:
        fixture['requestCalls'] += 1
        native_return(uc)
    elif address == 0xE2DEB0:
        fixture['errors'].append(uc.reg_read(UC_X86_REG_RDX) & 0xFFFFFFFF)
        native_return(uc)
    elif address == 0x3F7D80:
        native_return(uc, 0x3980000)
    elif address == 0x5A3F60:
        native_return(uc, 0x3981000)
leaf_hook = u.hook_add(UC_HOOK_CODE, provider_leaf)
report['nativePollCases'] = []
for name, ready, failed, requests, errors in [
    ('bdNet pending', [False, True], [False, False], 0, []),
    ('Steam pending', [True, False], [False, False], 0, []),
    ('both ready', [True, True], [False, False], 1, []),
    ('bdNet failed', [False, True], [True, False], 0, [0x90]),
    ('Steam failed', [True, False], [False, True], 0, [0x90]),
]:
    fixture.update(ready=ready, failed=failed, requestCalls=0, errors=[], signonResultCalls=0)
    u.mem_write(state, bytes(0x100))
    u.reg_write(UC_X86_REG_RCX, state)
    run(0x1073380)
    assert fixture['requestCalls'] == requests and fixture['errors'] == errors, (name, fixture)
    report['nativePollCases'].append({'case': name, 'requestCalls': requests, 'errorReasons': errors})
fixture.update(ready=[False, False], failed=[False, False], ticks=[])
u.mem_write(0x3980000, b'\x01')
run(0x4064A0, 0x406562)
assert fixture['ticks'] == [0, 1]
report['nativeTickWhilePending'] = fixture['ticks']
report['nativeInitResetCases'] = []
for name, results, expected_init, expected_reset, active in [
    ('both initialized', [True, True], [0, 1], [], True),
    ('bdNet initialization failed', [False, True], [0], [], False),
    ('Steam initialization failed', [True, False], [0, 1], [0], False),
]:
    fixture.update(initResults=results, initCalls=[], resetCalls=[])
    u.mem_write(0x3980000, b'\x01'+bytes(0x200))
    run(0x407580)
    assert fixture['initCalls'] == expected_init and fixture['resetCalls'] == expected_reset
    assert bool(u.mem_read(0x3980001, 1)[0]) == active
    report['nativeInitResetCases'].append({'case': name, 'initCalls': expected_init,
        'rollbackCalls': expected_reset, 'nativeActive': active})
fixture.update(initCalls=[], resetCalls=[])
u.mem_write(0x3980000, b'\x01\x01'+bytes(0x200))
run(0x4074E0)
assert fixture['resetCalls'] == [1, 0] and u.mem_read(0x3980001, 1)[0] == 0
report['nativeShutdownOrder'] = fixture['resetCalls']
u.hook_del(leaf_hook)
if len(sys.argv) > 2:
    Path(sys.argv[2]).write_text(json.dumps(report, indent=2)+'\n')
print('PASS: native provider predicates, 256 Steam readiness cases, pending-provider pumps, SignOn wait/request/failure, and init/reset rollback')
