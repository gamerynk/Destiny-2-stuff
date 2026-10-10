from pathlib import Path
import argparse, json, struct, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "crucible"))
from package_reader import Packages, array, u32

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", type=Path, required=True)
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--capture-buckets", action="store_true")
    args = ap.parse_args()
    p = Packages(args.game, args.pid)
    root_tag = 0x81327D22
    root = p.read(root_tag)[1]
    item_tag, list_tag = (u32(root, 8 + slot * 16) for slot in (48, 97))
    item_cls, items = p.read(item_tag)
    list_cls, lists = p.read(list_tag)
    assert (item_cls, list_cls) == (0x80807BE4, 0x80807A78)
    ni, si, _ = array(items, 8, 0x80807BE8, 24)
    nl, sl, _ = array(lists, 8, 0x80807A7E, 24)
    indices = (1, 2, 3, 5, 6, 7, 9, 10, 11)
    subclasses = {}
    for i in range(ni):
        h, tag = u32(items, si+i*24), u32(items, si+i*24+16)
        data = p.read(tag)[1]
        if len(data) < 136:
            continue
        relative = struct.unpack_from('<q', data, 128)[0]
        if relative and 0 <= 128+relative <= len(data)-12:
            index = struct.unpack_from('<H', data, 128+relative)[0]
            if index in indices:
                assert index not in subclasses
                subclasses[index] = h
    assert len(subclasses) == 9
    args.output.mkdir(parents=True, exist_ok=True)
    if args.capture_buckets:
        (args.output/'pools').mkdir(exist_ok=True)
    rows, provenance = set(), []
    for index, subclass in subclasses.items():
        tag = u32(lists, sl+index*24+16)
        data = p.read(tag)[1]
        n, start, _ = array(data, 16, 0x80807A86, 64)
        assert n == 24
        if args.capture_buckets:
            (args.output/f'subclass-list-{index}.bin').write_bytes(data)
        for node in range(n):
            entry = start+node*64
            assert struct.unpack_from('<q', data, entry+32)[0] == 0, 'Unexpected live selector'
            pool = u32(data, entry+56)
            cls, blob = p.read(pool)
            assert cls == 0x80807A8B
            ng, group, _ = array(blob, 8, 0x80807A8D, 144)
            assert ng == 1, 'Unexpected subgroup layout'
            nv, variant, _ = array(blob, group, 0x80807A97, 88)
            assert nv > 0
            np, perks, _ = array(blob, variant+(nv-1)*88+32, 0x80807C5F, 2)
            values = struct.unpack_from('<'+'H'*np, blob, perks) if np else ()
            assert all(v < 0xFFFF for v in values)
            rows.update((subclass, node, v) for v in values)
            provenance.append(dict(subclass=f'{subclass:08X}', list=index, node=node,
                                   pool=f'{pool:08X}', variant=nv-1, perks=values))
            if args.capture_buckets:
                (args.output/'pools'/f'{pool:08X}.bin').write_bytes(blob)
    assert len(rows) == 209, 'Package version or mapping changed; review before publishing'
    (args.output/'subclass_perks.txt').write_text(
        '# Extracted PC86657 subclass node perks.\n'+''.join(f'{h:08X} {n} {v}\n' for h,n,v in sorted(rows)))
    (args.output/'pc86657_subclass_perks.inc').write_text(
        '// Generated from PC 86657 investment root 0x81327D22.\n'
        '// Reproduce with tools/content/export_subclass_perks.py.\n'+
        ''.join(f'    {{0x{h:08X}U, {n}, {v}}},\n' for h,n,v in sorted(rows)))
    (args.output/'provenance.json').write_text(json.dumps(dict(root=f'{root_tag:08X}', nodes=provenance),indent=2))
    print(f'PASS: {len(rows)} perks across {len(subclasses)} subclasses')
if __name__ == '__main__':
    main()
