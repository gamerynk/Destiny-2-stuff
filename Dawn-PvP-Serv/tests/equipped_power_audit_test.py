from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/content'))
from audit_equipped_power import prefix


class PowerPrefix(unittest.TestCase):
    def test_character_boundaries_and_authored_socket_lanes(self):
        data=bytearray(struct.pack('<QIII',0,0,0,2))
        for index in range(2):
            data.extend(struct.pack('<QBBBBBBBfIBBBBBB',0,index,0,0,1,50,1,1,1.,0,0,6,7,10,11,2))
            for slot in range(16):
                data.append(slot<3)
                if slot>=3: continue
                data.extend(struct.pack('<QIiiiIBI',0,0x100+slot,106-index,1,1,0,1,1))
                for lane in range(12):
                    data.append(lane==0)
                    if lane==0:data.extend(struct.pack('<I',0x12345678))
            data.extend(struct.pack('<IiB',0,17,index))
            if index:data.extend(bytes(36))
        decoded=prefix(data,7)
        self.assertFalse(decoded[0]['selected']);self.assertTrue(decoded[1]['selected'])
        self.assertEqual(decoded[0]['equipment']['heavy']['derivedPower'],1060)
        self.assertEqual(decoded[1]['equipment']['kinetic']['level'],105)
        self.assertEqual(decoded[1]['equipment']['energy']['plugs'],['12345678']+[None]*11)


if __name__=='__main__':unittest.main()
