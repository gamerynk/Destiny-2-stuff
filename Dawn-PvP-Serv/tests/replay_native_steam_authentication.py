import hashlib, json, struct, sys
from pathlib import Path
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

path = Path(sys.argv[1])
data = path.read_bytes()
pe = pefile.PE(data=data)
base = pe.OPTIONAL_HEADER.ImageBase
image = pe.get_memory_mapped_image()
local = 0x10000000000
obj, utils, serialized = local+0x10000, local+0x20000, local+0x21000
vt, sdkvt, secure_time, request = local+0x22000, local+0x23000, local+0x24000, local+0x25000
stop = local+0x26000
cases=[]
for name, ready, provider, call_handle, valid_cert in [
    ('IPC_not_ready',0,True,7,False),
    ('serialized_provider_missing',1,False,7,False),
    ('certificate_request_failed',1,True,0,False),
    ('certificate_request_queued',1,True,7,False),
    ('current_signed_certificate',1,True,7,True),
    ('cached_certificate_refresh_failed',1,True,0,True),
]:
    u=Uc(UC_ARCH_X86,UC_MODE_64)
    u.mem_map(base,(len(image)+0xFFF)&~0xFFF);u.mem_write(base,image)
    u.mem_map(local,0x30000)
    def q(address,value):u.mem_write(address,struct.pack('<Q',value))
    def d(address,value):u.mem_write(address,struct.pack('<I',value&0xffffffff))
    q(obj+8,utils);q(utils,vt);q(vt+0xC0,secure_time)
    d(obj+0x7BB8,ready);d(obj+0x278,0x10);q(obj+0x280,0x011000011CD7F62D)
    if provider:q(obj+0x7BA0,serialized)
    q(serialized,sdkvt);q(sdkvt+0x10,request)
    if valid_cert:
        d(obj+0x20,5);d(obj+0x58,0x82);d(obj+0x39C,100)
        d(obj+0xA8,1001 if name=='cached_certificate_refresh_failed' else 10000)
    sp=local+0xF008;q(sp,stop);u.reg_write(UC_X86_REG_RSP,sp);u.reg_write(UC_X86_REG_RCX,obj)
    item={'case':name,'requestCalls':0,'queuedHandles':[],'nativeAvailabilityReports':[]}
    def ret(uc,value=0):
        rsp=uc.reg_read(UC_X86_REG_RSP);target=struct.unpack('<Q',uc.mem_read(rsp,8))[0]
        uc.reg_write(UC_X86_REG_RSP,rsp+8);uc.reg_write(UC_X86_REG_RAX,value);uc.reg_write(UC_X86_REG_RIP,target)
    def code(uc,address,size,user):
        rva=address-base
        if address==secure_time:ret(uc,1000)
        elif address==request:
            item['requestCalls']+=1;ret(uc,call_handle)
        elif rva in [0xF18E0,0xAC1B0,0x2DE80,0xF4EA0,0xF72C0]:ret(uc)
        elif rva==0xD78E0:ret(uc,1)
        elif rva==0x8F730:
            value=uc.reg_read(UC_X86_REG_RDX)&0xffffffff
            item['nativeAvailabilityReports'].append(value if value<0x80000000 else value-0x100000000)
            ret(uc)
        elif rva==0x90020:
            source=uc.reg_read(UC_X86_REG_RCX);output=uc.reg_read(UC_X86_REG_RDX)
            uc.mem_write(output,bytes(uc.mem_read(source,8))+struct.pack('<i',-1));ret(uc)
        elif rva==0xD9550:
            item['queuedHandles'].append(uc.reg_read(UC_X86_REG_R9));ret(uc)
        elif not base<=address<base+len(image):
            raise RuntimeError('Unsupported external transition '+hex(address))
    u.hook_add(UC_HOOK_CODE,code)
    u.emu_start(base+0xD6D20,stop,count=20000)
    assert u.reg_read(UC_X86_REG_RIP)==stop,item
    cases.append(item)
assert cases[0]['requestCalls']==0 and cases[0]['nativeAvailabilityReports']==[2]
assert cases[1]['nativeAvailabilityReports']==[-102]
assert cases[2]['requestCalls']==1 and cases[2]['nativeAvailabilityReports']==[-101]
assert cases[3]['queuedHandles']==[7] and cases[3]['nativeAvailabilityReports']==[3]
assert cases[4]['requestCalls']==0 and cases[4]['nativeAvailabilityReports']==[]
assert cases[5]['requestCalls']==1 and cases[5]['nativeAvailabilityReports']==[100]
report={'module':str(path),'sha256':hashlib.sha256(data).hexdigest(),
        'producerRva':'0xd6d20','cases':cases,'targetInvocations':0,'targetWrites':0,
        'limitation':'Native producer replay with controlled IPC/identity/time/availability leaves; not an end-to-end Steam service or fresh game boot.'}
Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: native Steam auth waits, refuses failed request, queues real request, and preserves current signed certificate')
