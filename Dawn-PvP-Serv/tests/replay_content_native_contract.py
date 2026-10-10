from pathlib import Path
import json, struct, hashlib, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

assets = Path(sys.argv[1])
text = (assets/'native-text.bin').read_bytes()
report = {'text_sha256':hashlib.sha256(text).hexdigest(), 'cases':[]}
def machine():
    u=Uc(UC_ARCH_X86,UC_MODE_64)
    u.mem_map(0x1000,0x4000000)
    u.mem_write(0x1000,text)
    return u
def put(u,a,n,fmt='<Q'): u.mem_write(a,struct.pack(fmt,n))
def read(u,a,fmt='<Q'): return struct.unpack(fmt,u.mem_read(a,struct.calcsize(fmt)))[0]
def return_call(u,value=0):
    sp=u.reg_read(UC_X86_REG_RSP)
    u.reg_write(UC_X86_REG_RAX,value)
    u.reg_write(UC_X86_REG_RIP,read(u,sp))
    u.reg_write(UC_X86_REG_RSP,sp+8)

for capacity in [0,64,0x14F848,0x100000040]:
    u=machine();u.mem_write(0x39B710,bytes.fromhex('48895c2418'))
    stack=0x3800000;result=0x3900000;url=0x3900100;output=0x3901000
    put(u,0x267C358,0x3904000)
    u.mem_write(url,b'https://configured.example/config\x00')
    put(u,stack,0x3700000);put(u,stack+0x28,0xD00D);put(u,stack+0x30,0, '<B');put(u,stack+0x38,1,'<B')
    u.reg_write(UC_X86_REG_RSP,stack);u.reg_write(UC_X86_REG_RCX,result)
    u.reg_write(UC_X86_REG_RDX,url);u.reg_write(UC_X86_REG_R8,output);u.reg_write(UC_X86_REG_R9,capacity)
    captured=[]
    def hook(u,address,size,user):
        if address==0x2F8700:
            u.mem_write(u.reg_read(UC_X86_REG_RCX),bytes(u.mem_read(u.reg_read(UC_X86_REG_RDX),34)))
            return_call(u)
        elif address==0x39AAD0:
            captured.append(bytes(u.mem_read(u.reg_read(UC_X86_REG_RDX),0x280)));return_call(u,1)
        elif address==0x187C480: return_call(u)
    u.hook_add(UC_HOOK_CODE,hook)
    u.emu_start(0x39B710,0x3700000,count=1000)
    assert len(captured)==1
    d=captured[0]
    assert struct.unpack_from('<I',d,8)[0]==0
    assert struct.unpack_from('<Q',d,0x10)[0]==output
    assert struct.unpack_from('<q',d,0x18)[0]==capacity
    assert struct.unpack_from('<Q',d,0x20)[0]==0xD00D
    assert struct.unpack_from('<Q',d,0x28)[0]==0
    assert read(u,result+8,'<I')==1
    report['cases'].append({'native_get_capacity':capacity,'queued_output':True,'pending':True})

for count,status in [(0,200),(73,200),(0,503)]:
    u=machine();stack=0x3800000;result=0x3900000;wrapper=0x3902000;vtable=0x3903000
    put(u,wrapper,vtable);put(u,vtable+0x68,0x3600000);put(u,vtable+0x18,0x3600010)
    put(u,wrapper+64,2,'<I');put(u,wrapper+92,count,'<I');put(u,wrapper+1216,status,'<I')
    put(u,stack+0x30,result);put(u,stack+0x38,0,'<I');put(u,result+8,1,'<I')
    u.reg_write(UC_X86_REG_RSP,stack);u.reg_write(UC_X86_REG_RBX,wrapper)
    u.reg_write(UC_X86_REG_R12,0);u.reg_write(UC_X86_REG_R14,0)
    def worker(u,address,size,user):
        if address==0x1A861A0: return_call(u,read(u,wrapper+92,'<I'))
        elif address==0x3600000:return_call(u,read(u,wrapper+1216,'<I'))
        elif address==0x3600010:return_call(u,read(u,wrapper+64,'<I'))
    u.hook_add(UC_HOOK_CODE,worker)
    u.emu_start(0x39B5A1,0x39B630,count=1000)
    assert read(u,result)==count and read(u,result+8,'<I')==0
    assert read(u,result+12,'<I')==2 and read(u,result+16,'<I')==status
    report['cases'].append({'worker_bytes':count,'http_status':status,'pending_cleared':True})

for gate,expected in [(0,0x4CA3F8),(1,0x4CA380)]:
    u=machine();u.mem_write(0x1F91698,bytes([gate]));seen=[]
    def branch(u,address,size,user):
        if address in [0x4CA3F8,0x4CA380]:seen.append(address);u.emu_stop()
    u.hook_add(UC_HOOK_CODE,branch);u.emu_start(0x4CA377,0x4CA400,count=10)
    assert seen==[expected]
    report['cases'].append({'signature_gate':gate,'native_continuation':hex(expected)})

out=Path(sys.argv[2]);out.write_text(json.dumps(report,indent=2)+'\n')
print('PASS: captured native GET descriptor union, 64-bit capacities, worker completion, and raw manifest signature branch')
