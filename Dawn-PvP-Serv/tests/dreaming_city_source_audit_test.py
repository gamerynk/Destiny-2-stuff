from pathlib import Path
import struct,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/content'))
from audit_dreaming_city_sources import declarations,decode

class Audit(unittest.TestCase):
    def test_exact_omissions(self):
        rows=declarations(Path('../reference/production/Dawn-production/Dawn/src/state/activity/coo/open_world_catalog.h'))
        self.assertEqual(len(rows),45)
        missing=[r for r in rows if not r['policyBound']]
        self.assertEqual(len(missing),24)
        self.assertTrue(all(r['registryIndex']<18 and 1<=r['slot']<=3 for r in missing))
        self.assertEqual(len({(r['registry'],r['slot']) for r in rows}),45)

    def fixture(self):
        data=bytearray(1024);definition=64
        struct.pack_into('<q',data,24,definition-24)
        struct.pack_into('<I',data,definition-4,0x8080948F)
        struct.pack_into('<IHH',data,definition+0x30,0x11,1,2)
        struct.pack_into('<Qq',data,definition+0xA8,1,8)
        header=definition+0xA8+16
        struct.pack_into('<QI',data,header,1,0x80808356)
        category=header+16;struct.pack_into('<I',data,category,0x33)
        struct.pack_into('<Qq',data,category+8,1,512-(category+16))
        struct.pack_into('<QI',data,512,1,0x80808358)
        choice=528;struct.pack_into('<q',data,choice,800-choice)
        struct.pack_into('<I',data,choice+12,10)
        struct.pack_into('<II',data,796,0x808099D8,0x80ABCDEF)
        class Reader:
            def read(self,tag):return 0x80809C36,bytes(data)
        return Reader(),dict(descriptor='80F1D6A9',registry='00000011',slot=2)

    def test_exact_categories_without_quota_inference(self):
        reader,row=self.fixture();result=decode(reader,row)
        self.assertEqual(result['categories'][0]['choices'][0]['entity'],'80ABCDEF')
        self.assertEqual(result['categories'][0]['choices'][0]['memberOffset'],736)
        self.assertIsNone(result['nativeQuota'])

    def test_native_identity_mismatch_rejected(self):
        reader,row=self.fixture();row['slot']=1
        with self.assertRaises(ValueError):decode(reader,row)

if __name__=='__main__':unittest.main()
