import unittest
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_64,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RDI,UC_X86_REG_RDX,UC_X86_REG_RSP,UC_X86_REG_RIP

class MissingSlice(unittest.TestCase):
 def test_absent_indices_complete_native_loader(self):
  data=(Path(__file__).resolve().parents[1]/'build/crash-analysis/code-91692-134354747061191738/text-rva00001000.bin').read_bytes()
  code=data[0xb47794-0x1000:0xb47920-0x1000]
  self.assertEqual(code[:6],bytes.fromhex('8b542420b905'))
  for index in (512,513,1023,0xffffffff):
   with self.subTest(index=index):
    u=Uc(UC_ARCH_X86,UC_MODE_64);u.mem_map(0xb47000,0x1000);u.mem_write(0xb47794,code)
    u.mem_map(0x35d000,0x1000);u.mem_write(0x35dc50,bytes.fromhex('31c0c3'))
    u.mem_map(0x10000000,0x1000);u.mem_map(0x20000000,0x10000)
    u.reg_write(UC_X86_REG_RDI,0x10000000);u.reg_write(UC_X86_REG_RSP,0x20008000)
    u.mem_write(0x20008020,index.to_bytes(4,'little'));u.mem_write(0x10000100,(3).to_bytes(4,'little'))
    def stop(machine,address,size,user):
     if address==0xb47918:machine.emu_stop()
    u.hook_add(UC_HOOK_CODE,stop);u.emu_start(0xb47794,0xb47920,count=100)
    self.assertEqual(u.reg_read(UC_X86_REG_RIP),0xb47918)
    self.assertEqual(int.from_bytes(u.mem_read(0x10000100,4),'little'),5)

if __name__=='__main__':unittest.main()
