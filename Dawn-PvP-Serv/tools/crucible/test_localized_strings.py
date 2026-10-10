import struct
import unittest

from localized_strings import Strings


class FixturePackages:
    def __init__(self):
        self.keys = bytearray(128)
        struct.pack_into('<Qq', self.keys, 8, 3, 64 - 16)
        struct.pack_into('<I', self.keys, 24, 2)
        struct.pack_into('<QI', self.keys, 64, 3, 0x80800070)
        struct.pack_into('<3I', self.keys, 80, 10, 20, 30)
        self.english = bytearray(512)
        struct.pack_into('<Qq', self.english, 8, 2, 96 - 16)
        struct.pack_into('<QI', self.english, 96, 2, 0x80809A90)
        struct.pack_into('<Qq', self.english, 72, 3, 192 - 80)
        struct.pack_into('<QI', self.english, 192, 3, 0x80809A8E)
        struct.pack_into('<qQ', self.english, 208, 0, 0)
        struct.pack_into('<qQ', self.english, 224, 112 - 224, 2)
        struct.pack_into('<qQ', self.english, 240, 144 - 240, 1)
        self.english[320:326] = b'The AB'
        for row, at, length, bias in [(112, 320, 4, 0), (144, 324, 2, 1)]:
            struct.pack_into('<q', self.english, row + 8, at - row - 8)
            struct.pack_into('<H', self.english, row + 20, length)
            struct.pack_into('<H', self.english, row + 24, bias)

    def read(self, tag):
        return (0x80809A88, self.keys) if tag == 1 else (0x80809A8A, self.english)


class LocalizedStringsTests(unittest.TestCase):
    def test_empty_shared_multipart_and_bias(self):
        self.assertEqual(Strings(FixturePackages()).bank(1),
                         {10: '', 20: 'The BC', 30: 'BC'})

    def test_rejects_part_reference_outside_part_array(self):
        fixture = FixturePackages()
        struct.pack_into('<q', fixture.english, 224, 320 - 224)
        with self.assertRaisesRegex(ValueError, 'part reference'):
            Strings(fixture).bank(1)

    def test_rejects_part_payload_outside_file(self):
        fixture = FixturePackages()
        struct.pack_into('<q', fixture.english, 120, 9000)
        with self.assertRaisesRegex(ValueError, 'payload'):
            Strings(fixture).bank(1)


if __name__ == '__main__':
    unittest.main()
