from pathlib import Path
import hashlib,json,struct,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64,UC_HOOK_CODE
from unicorn.x86_const import *
assets=Path(sys.argv[1]);text=(assets/'native-text.bin').read_bytes();rdata=(assets/'native-rdata.bin').read_bytes()
report={'text_sha256':hashlib.sha256(text).hexdigest(),'cases':[]}
def put(u,a,n,fmt='<Q'):u.mem_write(a,struct.pack(fmt,n))
def read(u,a,fmt='<Q'):return struct.unpack(fmt,u.mem_read(a,struct.calcsize(fmt)))[0]
def ret(u,value=0):
    sp=u.reg_read(UC_X86_REG_RSP);u.reg_write(UC_X86_REG_RAX,value)
    u.reg_write(UC_X86_REG_RIP,read(u,sp));u.reg_write(UC_X86_REG_RSP,sp+8)
def cstr(u,a):
    data=bytes(u.mem_read(a,2048));return data.split(b'\0')[0].decode()
def start(u,rva,args,extra=()):
    stack=0x3800000;put(u,stack,0x3700000)
    for reg,value in zip([UC_X86_REG_RCX,UC_X86_REG_RDX,UC_X86_REG_R8,UC_X86_REG_R9],args):u.reg_write(reg,value)
    for n,value in enumerate(extra):put(u,stack+0x28+n*8,value)
    u.reg_write(UC_X86_REG_RSP,stack);u.emu_start(rva,0x3700000,count=1000)
for method in [6,8]:
    u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(0x1000,0x4000000);u.mem_write(0x1000,text);u.mem_write(0x1B91000,rdata)
    wrapper=0x3900000;body=0x3910000;url=0x3920000;output=0x3930000;content_type=0x3940000
    header_pairs=0x3950000;key=0x3951000;value=0x3952000;vtable=0x3960000
    u.mem_write(body,b'ABC');u.mem_write(url,b'https://original.example/SignOn?opaque=kept\0')
    u.mem_write(content_type,b'application/x-protobuf\0');u.mem_write(key,b'Authorization\0');u.mem_write(value,b'Bearer native-header\0')
    put(u,header_pairs,key);put(u,header_pairs+8,value);put(u,wrapper,vtable);put(u,vtable+0x60,0x3600000)
    opts=[];headers=[];fail_url=False;curl_closed=[]
    def hook(u,address,size,user):
        if address==0x1A86700:put(u,wrapper+0x48,0xDEAD);put(u,wrapper+0x40,5,'<I');ret(u)
        elif address==0x1AB89D0:
            option=u.reg_read(UC_X86_REG_RDX)&0xffffffff;val=u.reg_read(UC_X86_REG_R8)
            opts.append((option,val));ret(u,3 if fail_url and option==10002 else 0)
        elif address==0x1AB8940:curl_closed.append(u.reg_read(UC_X86_REG_RCX));ret(u)
        elif address==0x187CB30:ret(u,u.reg_read(UC_X86_REG_RAX))
        elif address==0x1A831C0:
            dest=u.reg_read(UC_X86_REG_RCX);fmt=cstr(u,u.reg_read(UC_X86_REG_R8));arg=u.reg_read(UC_X86_REG_R9)
            if '%s' in fmt:
                if fmt.count('%s')==2:formatted=fmt%(cstr(u,arg),cstr(u,read(u,u.reg_read(UC_X86_REG_RSP)+0x28)))
                else:formatted=fmt%cstr(u,arg)
            else:formatted=fmt.replace('%I64u','%d')%arg
            u.mem_write(dest,formatted.encode()+b'\0');ret(u,len(formatted))
        elif address==0x1AB8CC0:headers.append(cstr(u,u.reg_read(UC_X86_REG_RDX)));ret(u,0x3970000)
        elif address in [0x187C480,0x380850,0x1B7FB20,0x3600000]:ret(u)
        elif address==0x187E85C:
            dest=u.reg_read(UC_X86_REG_RCX);source=u.reg_read(UC_X86_REG_RDX);count=u.reg_read(UC_X86_REG_R8)
            u.mem_write(dest,bytes(u.mem_read(source,count)));ret(u,dest)
        elif address==0x1A90CF0:ret(u)
        elif address==0x1A85040:ret(u,1)
    u.hook_add(UC_HOOK_CODE,hook)
    start(u,0x1A85310,[wrapper,method,body,3 if method==6 else 0])
    if method==6:
        assert read(u,wrapper+0x88)==body and read(u,wrapper+0x90,'<I')==3
        assert (47,1) in opts and (30115,3) in opts
    else:assert (80,1) in opts
    start(u,0x1A85430,[wrapper,content_type,0,1],[header_pairs])
    assert method==8 or 'Content-Type: application/x-protobuf' in headers
    assert 'Authorization: Bearer native-header' in headers
    start(u,0x1A85590,[wrapper,method,url,output],[64,0])
    assert (10002,url) in opts and (81,2) in opts and (64,1) in opts
    assert read(u,wrapper+0x70)==output and read(u,wrapper+0x78,'<I')==64
    incoming=0x3980000;u.mem_write(incoming,b'abcdefghijk');put(u,wrapper+0x78,8,'<I')
    start(u,0x1A85E30,[incoming,1,3,wrapper]);assert u.reg_read(UC_X86_REG_RAX)==3
    start(u,0x1A85E30,[incoming+3,1,5,wrapper]);assert u.reg_read(UC_X86_REG_RAX)==5
    assert bytes(u.mem_read(output,8))==b'abcdefgh' and read(u,wrapper+0x5c,'<I')==8
    start(u,0x1A85E30,[incoming+8,1,1,wrapper]);assert u.reg_read(UC_X86_REG_RAX)==0
    assert read(u,wrapper+0x5c,'<I')==8 and bytes(u.mem_read(output,8))==b'abcdefgh'
    report['cases'].append({'method':'POST'if method==6 else'GET',
        'curl_url':cstr(u,url),'headers':headers,'TLS_verify_host':2,'TLS_verify_peer':1,
        'body_pointer_retained':method==6,'output_capacity':64,'native_write_boundary_and_overflow':True,
        'options':[(o,hex(v))for o,v in opts]})
    fail_url=True;put(u,wrapper+0x48,0xDEAD)
    start(u,0x1A85590,[wrapper,method,url,output],[64,0])
    assert u.reg_read(UC_X86_REG_RAX)==3 and read(u,wrapper+0x48)==0 and curl_closed==[0xDEAD]
    report['cases'].append({'method':'POST'if method==6 else'GET','URL_guard_failure':3,
                            'native_curl_handle_closed':True,'native_transfer_not_started':True})
Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: native curl GET/POST method, exact URL/query, binary body, native header list, response buffer and verified TLS options')
