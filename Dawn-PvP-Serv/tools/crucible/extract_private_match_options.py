import argparse
import json
import struct
from pathlib import Path
from package_reader import Packages, array, u32

TAG = 0x81613CFB
TITLES = {0xC32D3EE2: 'score', 0xE2E1176C: 'seconds'}

def extract(data):
    count, start, _ = array(data, 8, 0x808059AC, 48)
    result = []
    for index in range(count):
        row = start + index * 48
        kind = TITLES.get(u32(data, row + 16))
        if not kind:
            continue
        n, choices, _ = array(data, row + 32, 0x808059AE, 28)
        values = [struct.unpack_from('<f', data, choices + i * 28)[0] for i in range(n)]
        if not 0 < n <= 15 or any(v <= 0 or v > 86400 or int(v) != v for v in values):
            raise ValueError('Unsupported native numeric match choice')
        result.append(dict(index=index, key=u32(data, row), kind=kind, values=list(map(int, values))))
    return result

def header(rows):
    out = ['// Generated from build 86657 native private-match menu tag 0x81613CFB.',
           '// Regenerate with tools/crucible/extract_private_match_options.py.',
           '#pragma once', '#include <array>', '#include <cstdint>',
           'namespace dawn::state::activity::crucible::menu_rules {',
           'struct Definition { std::int16_t index; bool seconds; std::uint8_t count;',
           '    std::array<std::uint32_t,15> values; };',
           f'inline constexpr std::array<Definition,{len(rows)}> kDefinitions{{{{']
    for row in rows:
        out.append('    {' + f"{row['index']}, {'true' if row['kind']=='seconds' else 'false'}, {len(row['values'])}, "
                   + '{' + ','.join(map(str,row['values'])) + '}},')
    return '\n'.join(out + ['}};', '}',''])

if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid',type=int,required=True)
    ap.add_argument('--game-root',type=Path,required=True)
    ap.add_argument('--header',type=Path,required=True)
    ap.add_argument('--metadata',type=Path,required=True)
    args=ap.parse_args()
    cls,data=Packages(args.game_root,args.pid).read(TAG)
    if cls != 0x808059A8: raise ValueError('Native menu class mismatch')
    rows=extract(data)
    args.header.write_text(header(rows))
    args.metadata.write_text(json.dumps({'tag':hex(TAG),'class':hex(cls),'choices':rows},indent=2)+'\n')
