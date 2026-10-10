import argparse
from pathlib import Path
import struct

from mapped_image import read_process_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--base', type=lambda text: int(text, 0), required=True,
                        help='MainModule.BaseAddress from the selected process')
    parser.add_argument('--class-id', type=lambda text: int(text, 0), required=True)
    parser.add_argument('--full', action='store_true',
                        help='show all bounded words of the linked .vmp0 descriptor')
    args = parser.parse_args()
    image = read_process_image(args.pid, args.game_root)
    needle = struct.pack('<I', args.class_id)
    refs = []
    offset = 0
    while (offset := image.find(needle, offset)) >= 0:
        if 0x1F11000 <= offset < 0x35CF000:
            refs.append(offset)
        offset += 1
    print(f'class=0x{args.class_id:08X} data_refs=' +
          ','.join(f'0x{ref:X}' for ref in refs))
    if len(refs) != 2:
        return
    row = max(refs)
    fields = struct.unpack_from('<I4xQQQ', image, row)
    print('registry', ' '.join(f'0x{value:X}' for value in fields))
    descriptor = fields[1] - args.base
    if 0 <= descriptor + 0x88 <= len(image):
        size = struct.unpack_from('<I', image, descriptor)[0]
        layout = None
        for first_field in (0x80, 0x88):
            count = struct.unpack_from('<I', image,
                                       descriptor + first_field - 0x20)[0]
            embedded = struct.unpack_from('<I', image,
                                          descriptor + first_field - 0x18)[0]
            if (size == first_field + count * 0x28 and count <= 128
                    and embedded == args.class_id
                    and descriptor + size <= len(image)):
                layout = (first_field, count)
                break
        if layout is not None:
            first_field, count = layout
            print(f'reflected_fields={count} object_bytes='
                  f'{struct.unpack_from("<I", image, descriptor + first_field - 0x14)[0]}')
            for field in range(count):
                offset_a, offset_b, presence_index, visibility, kind, sub, lo, hi, _, _ = \
                    struct.unpack_from('<10I', image,
                                       descriptor + first_field + field * 0x28)
                print(f'  field={field} offset_a=0x{offset_a:X} offset_b=0x{offset_b:X} '
                      f'kind={kind} sub=0x{sub:08X} '
                      f'lo=0x{lo:08X} hi=0x{hi:08X} '
                      f'presence_index=0x{presence_index:X} visibility=0x{visibility:X}')
    for index, pointer in enumerate(fields[1:]):
        rva = pointer - args.base
        if not 0 <= rva + 64 <= len(image):
            continue
        print(f'pointer_rva=0x{rva:X}')
        length = 64
        if index == 0 and args.full:
            length = min(0x400, (struct.unpack_from('<I', image, rva)[0] + 15) & ~15)
        for at in range(rva, rva + length, 16):
            values = struct.unpack_from('<4I', image, at)
            print(f'  0x{at:X}: ' + ' '.join(f'{value:08X}' for value in values))


if __name__ == '__main__':
    main()
