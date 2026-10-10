import argparse
from pathlib import Path
import struct

import pefile

from mapped_image import read_process_image


SCHEMAS = (0x8080992F, 0x8080954B, 0x8080955A, 0x8080956A,
           0x80804F67, 0x80804FCA)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--details', action='store_true',
                        help='print adjacent aligned words for selected metadata references')
    args = parser.parse_args()
    image = read_process_image(args.pid, args.game_root)
    pe = pefile.PE(str(args.game_root / 'destiny2.exe'), fast_load=True)
    sections = [(s.Name.rstrip(b'\0').decode('ascii', 'replace'),
                 s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize)
                for s in pe.sections]
    for schema in SCHEMAS:
        needle = struct.pack('<I', schema)
        positions = []
        offset = 0
        while (offset := image.find(needle, offset)) >= 0:
            section = next((name for name, start, end in sections
                            if start <= offset < end), '?')
            positions.append(f'{section}+0x{offset:X}')
            offset += 1
        print(f'0x{schema:08X}: {len(positions)} refs: ' + ', '.join(positions[:100]))
        if args.details:
            offset = 0
            shown = 0
            while (offset := image.find(needle, offset)) >= 0:
                if any(name == '.data' and start <= offset < end
                       for name, start, end in sections):
                    start = offset & ~15
                    for at in range(start - 32, start + 64, 16):
                        words = struct.unpack_from('<4I', image, at)
                        print(f'  rva=0x{at:X} words=' +
                              ' '.join(f'{word:08X}' for word in words))
                    shown += 1
                    if shown == 2:
                        break
                offset += 1


if __name__ == '__main__':
    main()
