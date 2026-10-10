from pathlib import Path
import hashlib, json, struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

assets=Path(sys.argv[1]); text=(assets/'native-text.bin').read_bytes()
profile=Path(sys.argv[2]).read_bytes()
report={'text_sha256':hashlib.sha256(text).hexdigest(),
        'profile_update_sha256':hashlib.sha256(profile).hexdigest(),'cases':[]}
def machine():
    u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(0x1000,0x4000000);u.mem_write(0x1000,text);return u
def put(u,a,n,fmt='<Q'):u.mem_write(a,struct.pack(fmt,n))
def read(u,a,fmt='<Q'):return struct.unpack(fmt,u.mem_read(a,struct.calcsize(fmt)))[0]
def ret(u,value=0):
    sp=u.reg_read(UC_X86_REG_RSP);u.reg_write(UC_X86_REG_RAX,value)
    u.reg_write(UC_X86_REG_RIP,read(u,sp));u.reg_write(UC_X86_REG_RSP,sp+8)

for cached,deadline,valid,expected in [(2**63-1,0,1,1),(2**63-1,99,1,1),
                                       (42,43,1,0),(43,43,1,0),(99,43,1,1),(99,43,0,0)]:
    u=machine();u.mem_write(0xFA7E60,bytes.fromhex('48895c2418'))
    stack=0x3800000;accessor=0x3900000;account=0x3901000;character=0x3910000;cache=0x3920000
    put(u,stack,0x3700000);put(u,accessor+8,account);put(u,accessor+0x18,character)
    put(u,cache,cached);put(u,cache+8,valid,'<B');put(u,character+0x4c08,1,'<B')
    put(u,character+0x4c10,deadline);put(u,account+8,1000)
    def hook(u,address,size,user):
        if address==0xFA5090:ret(u,cache)
    u.hook_add(UC_HOOK_CODE,hook);u.reg_write(UC_X86_REG_RSP,stack);u.reg_write(UC_X86_REG_RCX,accessor)
    u.emu_start(0xFA7E60,0x3700000,count=100)
    verdict=u.reg_read(UC_X86_REG_RAX)&0xff;assert verdict==expected
    report['cases'].append({'freshness_cached':cached,'selected_deadline':deadline,'valid':valid,'verdict':verdict})

for count,cap,expected in [(1,0,12),(1,1,18),(3,3,18),(4,3,12),(0,0,18)]:
    u=machine();stack=0x3800000;config=0x3900000;props=0x3901000
    put(u,stack+0x28,0x3700000);put(u,config+0x2b4,cap,'<i');put(u,props+0x14,count,'<i')
    u.reg_write(UC_X86_REG_RSP,stack);u.reg_write(UC_X86_REG_RDI,config);u.reg_write(UC_X86_REG_RBX,props)
    u.emu_start(0x17E396A,0x3700000,count=100);value=u.reg_read(UC_X86_REG_RAX)
    assert value==expected
    report['cases'].append({'composition_count':count,'native_cap':cap,'result':value})

for registry_index,expected_hash in [(0x200,0x811C9DC5),(5,0x8265820B)]:
    u=machine();u.mem_write(0xB447E0,bytes.fromhex('48895c2418'))
    stack=0x3800000;context=0x3900000;output=0x3901000;registry_result=0x3902000
    put(u,stack,0x3700000);put(u,context+0x104,0,'<I')
    put(u,0x1FB5F84,0x8265820B,'<I');put(u,registry_result,registry_index,'<I')
    def picker(u,address,size,user):
        if address==0x4294C0:ret(u,0x3903000)
        elif address==0x427380:ret(u,registry_result)
    u.hook_add(UC_HOOK_CODE,picker);u.reg_write(UC_X86_REG_RSP,stack)
    u.reg_write(UC_X86_REG_RCX,context);u.reg_write(UC_X86_REG_RDX,output)
    u.emu_start(0xB447E0,0x3700000,count=100)
    assert read(u,output,'<I')==expected_hash
    report['cases'].append({'orbit_registry_index':registry_index,'returned_hash':hex(expected_hash)})

for state,flag,expected_state,expected_exit in [(0,0,1,False),(0,1,1,False),(1,1,1,False),(9,0,9,True)]:
    u=machine();u.mem_write(0x1073B10,profile)
    stack=0x3800000;step=0x3900000;record=0x3901000;events=[]
    put(u,stack,0x3700000);put(u,step+0x38,state,'<I');put(u,record+0x10,flag,'<B')
    def update(u,address,size,user):
        if address in (0xE2E900,0xBB4820):ret(u,1)
        elif address in (0xE2E960,0xC24110,0x187C480):ret(u)
        elif address==0x46AA00:put(u,u.reg_read(UC_X86_REG_RCX),0xC0FFEE);ret(u)
        elif address==0xBE17E0:
            assert u.reg_read(UC_X86_REG_RCX)==0xC0FFEE;ret(u,record)
        elif address==0xBE16A0:events.append(('cached_key',u.reg_read(UC_X86_REG_RCX)));ret(u)
        elif address==0xE2DEA0:events.append(('handoff',u.reg_read(UC_X86_REG_RCX)));ret(u)
    u.hook_add(UC_HOOK_CODE,update);u.reg_write(UC_X86_REG_RSP,stack);u.reg_write(UC_X86_REG_RCX,step)
    u.emu_start(0x1073B10,0x3700000,count=200)
    assert read(u,step+0x38,'<I')==expected_state
    assert bool(events)==expected_exit
    if expected_exit:assert read(u,record+0x10,'<B')==1 and events==[('cached_key',0xC0FFEE),('handoff',25)]
    report['cases'].append({'profile_state':state,'seeded_local_flag':flag,'next_state':expected_state,'native_handoff':expected_exit})

Path(sys.argv[3]).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: native cache deadlines, composition caps, selected-character local profile completion')
