import hashlib, json, struct, sys
from pathlib import Path
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *
path=Path(sys.argv[1]);data=path.read_bytes();pe=pefile.PE(data=data)
base=pe.OPTIONAL_HEADER.ImageBase;image=pe.get_memory_mapped_image()
def rtti(vtable):
    locator=struct.unpack_from('<Q',image,vtable-8)[0]-base
    type_rva=struct.unpack_from('<I',image,locator+12)[0]
    return image[type_rva+16:].split(b'\0',1)[0].decode()
assert 'CMsgSteamDatagramCertificateRequest' in rtti(0x239878)
assert 'CMsgSteamDatagramCertificateSigned' in rtti(0x2397D8)
local=0x10000000000;stop=local+0xF000
cases=[]
for name, cert_len, sig_len, expected in [
    ('empty_cert',0,0,'No signed data'),
    ('request_has_no_ca_signature',4,0,'No signature'),
    ('signature_without_ca_id',4,64,'Missing CA Key ID'),
]:
    u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(base,(len(image)+4095)&~4095);u.mem_write(base,image)
    u.mem_map(local,0x10000)
    signed,cert,signature,inner,error=local+0x2000,local+0x3000,local+0x4000,local+0x5000,local+0x6000
    def q(a,v):u.mem_write(a,struct.pack('<Q',v))
    q(signed+0x18,cert);q(signed+0x28,signature);q(cert+0x10,cert_len);q(signature+0x10,sig_len)
    q(cert+0x18,15);q(signature+0x18,15)
    u.mem_write(cert,b'test');u.mem_write(inner,b'\xA5'*0x68)
    sp=local+0xE008;q(sp,stop);u.reg_write(UC_X86_REG_RSP,sp)
    for reg,value in [(UC_X86_REG_RCX,signed),(UC_X86_REG_RDX,inner),(UC_X86_REG_R8,1000),(UC_X86_REG_R9,error)]:u.reg_write(reg,value)
    parsed=[0]
    def ret(uc,value=0):
        rsp=uc.reg_read(UC_X86_REG_RSP);target=struct.unpack('<Q',uc.mem_read(rsp,8))[0]
        uc.reg_write(UC_X86_REG_RSP,rsp+8);uc.reg_write(UC_X86_REG_RAX,value);uc.reg_write(UC_X86_REG_RIP,target)
    def code(uc,address,size,user):
        rva=address-base
        if rva==0xE4FB0:ret(uc)
        elif rva==0x2FC30:
            source=uc.reg_read(UC_X86_REG_RDX);destination=uc.reg_read(UC_X86_REG_RCX)
            text=bytes(uc.mem_read(source,100)).split(b'\0',1)[0];uc.mem_write(destination,text+b'\0');ret(uc)
        elif rva==0x10E680:parsed[0]+=1;raise RuntimeError('Unsigned wrapper must not parse inner certificate')
        elif not base<=address<base+len(image):raise RuntimeError('Unsupported native transition '+hex(address))
    u.hook_add(UC_HOOK_CODE,code);u.emu_start(base+0xE4F00,stop,count=10000)
    actual=bytes(u.mem_read(error,100)).split(b'\0',1)[0].decode()
    assert u.reg_read(UC_X86_REG_RIP)==stop and u.reg_read(UC_X86_REG_RAX)==0
    assert parsed[0]==0 and bytes(u.mem_read(inner,0x68))==b'\xA5'*0x68 and actual==expected
    cases.append({'case':name,'error':actual,'authScope':False,'innerParsed':False,'innerUnchanged':True})
report={'module':str(path),'sha256':hashlib.sha256(data).hexdigest(),'entryRva':'0xe4f00',
        'requestConstructorRtti':rtti(0x239878),'signedConstructorRtti':rtti(0x2397D8),
        'cases':cases,'targetWrites':0,'targetInvocations':0,
        'limits':'Native verification/refusal with synthetic public protobuf storage; CA maintenance/string-copy leaves controlled. Does not replay full svc304 field3 origin or real private-key acceptance.'}
Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: native unsigned certificate wrappers refuse before inner parsing')
