from pathlib import Path
import hashlib, itertools, json, struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

text = (Path(sys.argv[1]) / 'native-text.bin').read_bytes()
report = {'native_text_sha256': hashlib.sha256(text).hexdigest(),
          'target_mutations': 0, 'hold_cases': [], 'producer_cases': []}
def machine():
    u = Uc(UC_ARCH_X86, UC_MODE_64)
    u.mem_map(0x1000, 0x4000000)
    u.mem_write(0x1000, text)
    return u
def put(u, a, value, fmt='<Q'):
    u.mem_write(a, struct.pack(fmt, value))
def read(u, a, fmt='<Q'):
    return struct.unpack(fmt, u.mem_read(a, struct.calcsize(fmt)))[0]
def ret(u, result=0):
    sp = u.reg_read(UC_X86_REG_RSP)
    u.reg_write(UC_X86_REG_RAX, result)
    u.reg_write(UC_X86_REG_RIP, read(u, sp))
    u.reg_write(UC_X86_REG_RSP, sp + 8)

assert text[0xEF44CA-0x1000:0xEF44D2-0x1000] == bytes.fromhex('488bd9e8aefbd2ff')
for bypass, pending, busy, elapsed in itertools.product([0,1], [0,1], [0,1], [-1,0,5000,5001,3600000]):
    u = machine(); owner=0x3900000; record=0x3910000; step=0x3920000; sp=0x3800000
    put(u, sp+0x28, 0x3700000); put(u, sp+0x30, 0)
    put(u, owner+0x200, 42 if pending else -1, '<q')
    put(u, step+0x38, -1 if elapsed == -1 else 4000000-elapsed, '<q')
    put(u, 0x280FE09, bypass, '<B'); put(u, 0x26B3920, 1, '<B'); put(u, 0x26B3928, 4000000)
    u.reg_write(UC_X86_REG_RSP, sp); u.reg_write(UC_X86_REG_RCX, step)
    def stub(u, address, size, data):
        if address == 0xE372E0: ret(u, owner)
        elif address == 0xC04030: ret(u, record)
        elif address == 0x352310:
            output=u.reg_read(UC_X86_REG_RDX)
            put(u, output, read(u, owner+0x200, '<i'), '<i')
            ret(u, output)
        elif address == 0xBFC0A0: ret(u, busy)
        elif address == 0x3700000: u.emu_stop()
    u.hook_add(UC_HOOK_CODE, stub); u.emu_start(0xEF44CA, 0x3700001, count=1000)
    hold=bool(u.reg_read(UC_X86_REG_RAX)&255)
    expected=not bypass and (not pending or (busy and (elapsed==-1 or elapsed<=5000)))
    assert hold == expected
    report['hold_cases'].append({'global_bypass':bypass,'native_pending_handle':pending,
                                'native_resource_busy':busy,'elapsed_ms':elapsed,'holds':hold})

for enabled, resolved in itertools.product([0,1], [0,1]):
    u=machine(); owner=0x3900000; tag=0x3910000; sp=0x3800000; dictionary=0x3930000
    put(u, sp, 0x3700000); put(u, owner+0x200, -1, '<q'); put(u, tag, 0x13A023BC, '<I')
    put(u, 0x1FB7F09, enabled, '<B')
    put(u, 0x1FA5390, dictionary); put(u, dictionary, 17, '<I')
    u.reg_write(UC_X86_REG_RSP, sp); u.reg_write(UC_X86_REG_RCX, owner); u.reg_write(UC_X86_REG_RDX, tag)
    published=[]
    def stub(u,address,size,data):
        if address==0xC4B720:
            output=u.reg_read(UC_X86_REG_RCX); put(u,output,5 if resolved else -1,'<i');ret(u,output)
        elif address==0x352310:
            output=u.reg_read(UC_X86_REG_RDX);put(u,output,-1,'<i');ret(u,output)
        elif address==0x557470:
            output=u.reg_read(UC_X86_REG_R8);put(u,output+0x18,1,'<i');ret(u,1)
        elif address==0x351C90:
            output=u.reg_read(UC_X86_REG_RCX);put(u,output,42);ret(u,output)
        elif address==0xC160AD:
            published.append(read(u,owner+0x200));u.emu_stop()
        elif address==0x3700000:u.emu_stop()
    u.hook_add(UC_HOOK_CODE,stub);u.emu_start(0xC15FF0,0x3700001,count=2000)
    value=read(u,owner+0x200,'<q')
    assert value==(42 if enabled and resolved else -1)
    report['producer_cases'].append({'native_data_gate':enabled,'native_asset_resolves':resolved,
                                     'pending_handle':value})
Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 40 native Orbit hold cases and 4 native destination producer gate/asset cases')

