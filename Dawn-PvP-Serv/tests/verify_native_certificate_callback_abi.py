import hashlib, json, struct, sys
from pathlib import Path
import pefile

path = Path(sys.argv[1]); data = path.read_bytes(); pe = pefile.PE(data=data)
image = pe.get_memory_mapped_image(); base = pe.OPTIONAL_HEADER.ImageBase
checks = {
    0xD7800: 'b818030000c3',                 # callback payload sizeof=792
    0xD7BEE: '448b06',
    0xD7C2E: '448b4604488d5608',
    0xD7CA5: '83be1002000040',
    0xD7CDA: '83be9402000040',
    0xD7D39: '488d96b8020000',
    0xD7E6D: '448b7e04',
    0xD7EB5: '488d5608',
    0xD7EC1: '488b8608020000',
    0xD7ED6: '8b9e10020000',
    0xD7F1D: '488d9614020000',
}
for rva, expected in checks.items():
    assert image[rva:rva+len(expected)//2].hex() == expected, (hex(rva), expected)
vtable = [struct.unpack_from('<Q', image, 0x24A790 + 8*i)[0]-base for i in range(4)]
assert vtable == [0xD89C0, 0xD89F0, 0xD7800, 0xD7BA0]
report = {
    'module': str(path), 'sha256': hashlib.sha256(data).hexdigest(),
    'consumerRva': '0xd7ba0', 'callbackVtableRva': '0x24a790', 'callbackBytes': 792,
    'publicFields': {'result': 0, 'certificateBytes': 4, 'certificate': 8,
                     'caKeyId': '0x208', 'signatureBytes': '0x210', 'signature': '0x214'},
    'privateRegionExcluded': '0x298..0x2d8 (native private import at0x2b8)',
    'instructionAssertions': len(checks), 'targetWrites': 0, 'targetInvocations': 0,
    'credentialPayloadsPersisted': False,
    'limits': 'Exact archived module ABI only; real native CA/key acceptance requires live parser result, not structural tests or HTTPack.',
    'publicWirePrimarySource': 'https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/src/common/steamnetworkingsockets_messages_certs.proto',
}
Path(sys.argv[2]).write_text(json.dumps(report, indent=2)+'\n')
print('PASS: native1296 callback ABI792, public certificate/CA/signature and excluded private region')
