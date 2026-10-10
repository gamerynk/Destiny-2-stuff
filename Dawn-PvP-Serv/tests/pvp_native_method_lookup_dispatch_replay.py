import os
"""Original native method resolver AND argument construction; controlled valid pools.
No helper/function success is stubbed. Stops before callback execution. Does not
claim a real pickup descriptor, allocator lifetime, or live callback selection.
"""
from pathlib import Path
import hashlib,json,struct,sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64,UC_HOOK_CODE
from unicorn.x86_const import *
path=Path(os.environ['DAWN_NATIVE_TEXT'])
text=path.read_bytes(); u=Uc(UC_ARCH_X86,UC_MODE_64)
u.mem_map(0x1000,(len(text)+4095)//4096*4096);u.mem_write(0x1000,text)
for start,size in [(0x1F96000,0x1000),(0x2439000,0x1000),(0x241F000,0x1000),(0x330E000,0x2000),(0x4000000,0x300000)]:u.mem_map(start,size)
def w32(a,v):u.mem_write(a,struct.pack('<I',v&0xFFFFFFFF))
def w64(a,v):u.mem_write(a,struct.pack('<Q',v&0xFFFFFFFFFFFFFFFF))
def r64(a):return struct.unpack('<Q',u.mem_read(a,8))[0]
header,tables,schemaRows,metadata,methodRows,methods=0x4000000,0x4010000,0x4030000,0x4040000,0x4050000,0x4060000
generations,allocation,bitmap,allocationOwner=0x4070000,0x4080000,0x4090000,0x40A0000
componentRows,descriptor,stack=0x4100000,0x4120000,0x42FF000
schema=0x80800001;schemaBank=0x400;nativeClass=3;methodBank=5
w64(0x2439C70,header);w64(header,tables);w32(header+0x10,0x40)
w32(0x241F7A0,0)
w64(0x330ED90,allocationOwner);w32(0x330ED90+0x34,schemaBank)
w64(allocationOwner+8,allocation);w32(allocation+8,16);w64(allocation+0x10,bitmap);w32(bitmap,1<<1)
t=schemaBank*0x40+tables
w64(t+8,schemaRows);w64(t+0x10,0x330ED90);w32(t+0x30,0x40);w32(t+0x34,0)
w32(schemaRows+0x40+0x14,0x80800002);u.mem_write(schemaRows+0x80+4,struct.pack('<H',nativeClass))
w64(0x1F965C8,generations);w32(0x1F965DC,0);w32(0x1F965E0,0x10);w32(0x1F965E4,0xFF);w64(0x1F965F4,methodBank)
w64(tables+methodBank*0x40+8,methodRows);w32(tables+methodBank*0x40+0x30,0x40);w32(tables+methodBank*0x40+0x34,0)
w64(methodRows+nativeClass*0x40,methods)
w32(descriptor,schema);w32(descriptor+8,1);w64(descriptor+0x10,0x30)
w64(tables+8,componentRows);w32(tables+0x30,0x100);w32(tables+0x34,0)
visited=set();steps=0
def observe(machine,address,size,data):
 global steps
 visited.add(address);steps+=1
 if address<0x1000 or address>=0x1B91000:raise RuntimeError(f'non-text execution {address:x}')
u.hook_add(UC_HOOK_CODE,observe)
cases=[]
for selected in (0,2,7):
 for callback in (0xE8A870,0xD0BF80,0xCE8F40):
  for methodSalt in (0,1,7):
   bank=methodBank|(methodSalt<<10)
   w32(generations+nativeClass*0x10,methodSalt)
   methodList=methods+methodSalt*0x100
   t=tables+bank*0x40
   w64(t+8,methodRows);w32(t+0x30,0x40);w32(t+0x34,0)
   w64(methodRows+nativeClass*0x40,methodList)
   for i in range(8):w64(methodList+i*16,0xDEAD0000+i)
   w64(methodList+selected*16,callback);w32(descriptor+4,selected)
   u.mem_write(stack-0x100,bytes(0x200))
   for register,value in [(UC_X86_REG_RCX,descriptor),(UC_X86_REG_RDX,42),(UC_X86_REG_R8,0x4121000),(UC_X86_REG_R9,0x4122000),(UC_X86_REG_RSP,stack)]:u.reg_write(register,value)
   before=steps;u.emu_start(0x1853640,0x18536C4,count=2000)
   assert u.reg_read(UC_X86_REG_RIP)==0x18536C4
   resolved=r64(u.reg_read(UC_X86_REG_RSP)+0x28)
   assert resolved==callback
   assert u.reg_read(UC_X86_REG_RCX)==componentRows+0x130
   assert [u.reg_read(r) for r in (UC_X86_REG_RDX,UC_X86_REG_R8,UC_X86_REG_R9)]==[42,0x4121000,0x4122000]
   cases.append({'method_index':selected,'method_salt':methodSalt,'actual_selected_callback_rva':hex(resolved),'steps':steps-before,'component_argument_matches':True})
required=[0xA40F40,0xA40210,0x33F480,0x187B9C0,0x350DE0,0xA40880,0xA40B50,0x9EB0B0,0x9EB310,0x187BA60,0x3523C0]
assert all(a in visited for a in required)
result={'pass':True,'text_sha256':hashlib.sha256(text).hexdigest(),'cases':cases,'helpers_executed_original':[hex(a) for a in required],'helper_stubs':0,'callback_literal_supplied_to_resolver':False,'stop_before_actual_callback':True,'target_calls':0,'target_writes':0,'scope':'Whole native1853640â†’A40F40â†’typed-schema/method-list lookup and component argument construction in controlled generation-valid synthetic pools; ordinary resolver mode0','unproved':['Real pickup descriptor schema/method index and owning native method table.','Other arena relocation modes, especially33DC30 branch with resolver mode1.','Object/descriptor lifetime and native allocation exclusion across F32CD0 original.','Callback execution or gameplay compatibility.']}
if len(sys.argv)!=2:raise SystemExit('usage: pvp_native_method_lookup_dispatch_replay.py NEW_OUTPUT_JSON')
with Path(sys.argv[1]).open('x',encoding='utf-8') as output:json.dump(result,output,indent=2)
print(f'PASS {len(cases)} whole native method lookup/dispatcher cases; zero helper stubs')
