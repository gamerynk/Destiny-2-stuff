import hashlib,json,sys
from pathlib import Path
import pefile
p=Path(sys.argv[1]); raw=p.read_bytes(); pe=pefile.PE(data=raw); image=pe.get_memory_mapped_image()
exports={x.name.decode():x.address for x in pe.DIRECTORY_ENTRY_EXPORT.symbols if x.name}
checks={"SteamAPI_ISteamNetworkingUtils_SetDebugOutputFunction":(0xF0440,"488b0148ff6068"),"SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus":(0xF03A0,"488b0148ff6008")}
for name,(rva,code) in checks.items():assert exports[name]==rva and image[rva:rva+7].hex()==code
setter="4885d2741285c97e0e48891520792300890d62772300c333c048890510792300890552772300c3"
assert image[0xF5C30:0xF5C30+39].hex()==setter
assert image[0xB1629:0xB162E].hex()=="4885db746a"
assert int.from_bytes(image[0x24BEC0+8:0x24BEC0+16],"little")==pe.OPTIONAL_HEADER.ImageBase+0xB1610
assert int.from_bytes(image[0x24BEC0+0x68:0x24BEC0+0x70],"little")==pe.OPTIONAL_HEADER.ImageBase+0xE1F30
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64
from unicorn.x86_const import UC_X86_REG_RCX,UC_X86_REG_RDX,UC_X86_REG_RSP
import struct
base=pe.OPTIONAL_HEADER.ImageBase
for level,callback,expected_level,expected_callback in [(5,0x100001234,5,0x100001234),(5,0,0,0),(0,0x100001234,0,0)]:
 u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(base,(len(image)+4095)&~4095);u.mem_write(base,image)
 stack=0x10000000000;u.mem_map(stack,0x2000);stop=stack+0x1000;sp=stack+0x808
 u.mem_write(sp,struct.pack('<Q',stop));u.reg_write(UC_X86_REG_RSP,sp)
 u.reg_write(UC_X86_REG_RCX,level);u.reg_write(UC_X86_REG_RDX,callback)
 u.emu_start(base+0xF5C30,stop,count=20)
 assert int.from_bytes(u.mem_read(base+0x32D560,8),'little')==expected_callback
 assert int.from_bytes(u.mem_read(base+0x32D3A8,4),'little')==expected_level
result={"imageSha256":hashlib.sha256(raw).hexdigest(),"targetInvocations":0,"targetWrites":0,"sdkBoundaries":list(checks),"setterNullRestoration":"clears callback and verbosity", "priorCallbackRva":"0x32D560","priorVerbosityRva":"0x32D3A8","relayStatusOptionalOutputNull":"accepted by native B1610","privateSetterReplayCases":3,"result":"PASS"}
if len(sys.argv)>2:Path(sys.argv[2]).write_text(json.dumps(result,indent=2)+"\n")
print("PASS: native flat SDK wrappers, relay optional output and debug callback restore ABI")
