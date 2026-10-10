import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re
import struct

from package_reader import Packages, array, u32


def fnv1(value):
    result = 0x811C9DC5
    for byte in value:
        result = ((result * 0x01000193) ^ byte) & 0xffffffff
    return result


def strings(blob):
    return {fnv1(name): name.decode('ascii') for name in
            re.findall(rb'[A-Za-z_][A-Za-z_0-9.\[\]]{3,}', blob)}


def children(cls, blob):
    if cls == 0x80809B14:
        return [u32(blob, 12)]
    if cls == 0x80809468:
        count, start, _ = array(blob, 16, None, 4)
        return [u32(blob, start + i * 4) for i in range(count)]
    return []


def slots_of(packages, object_tag):
    cls, blob = packages.read(object_tag)
    if cls != 0x80809462:
        return []
    key = u32(blob, 12)
    count, start, _ = array(blob, 32, None, 8)
    slots = [(u32(blob, start + i * 8), u32(blob, start + i * 8 + 4))
             for i in range(count)]
    names = {}
    try:
        bc, bs, _ = array(blob, 56, None, 24)
    except ValueError:
        return []
    pending = []
    for i in range(bc):
        at = bs + i * 24
        hc, hs, _ = array(blob, at + 8, None, 4)
        pending.extend(u32(blob, hs + j * 4) for j in range(hc))
    seen = set()
    for tag in pending:
        if tag in seen:
            continue
        seen.add(tag)
        cls, data = packages.read(tag)
        pending.extend(children(cls, data))
        if cls != 0x80809C36:
            continue
        candidates = strings(data)
        for base in range(0, len(data) - 128 + 1, 4):
            if u32(data, base) != tag or u32(data, base + 8) != 0x70 or u32(data, base + 48) != key:
                continue
            typ, ordinal = struct.unpack_from('<HH', data, base + 52)
            if ordinal >= len(slots) or typ != slots[ordinal][0]:
                continue
            names[ordinal] = {
                'name': candidates.get(slots[ordinal][1]),
                'tag': tag, 'component': u32(data, base + 4),
                'sense': u32(data, base + 68), 'auth': u32(data, base + 72),
            }
    return [{'object_tag': object_tag, 'key': key, 'index': i,
             'type': typ, 'name_hash': name_hash, **names.get(i, {})}
            for i, (typ, name_hash) in enumerate(slots)]


def registry_objects(packages, tag):
    cls, blob = packages.read(tag)
    if cls != 0x8080925E:
        raise ValueError('Bad registry class')
    for descriptor in (8, 24, 40):
        count, start, _ = array(blob, descriptor, 0x80809260, 4)
        for i in range(count):
            yield u32(blob, start + i * 4)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--scenario', type=lambda x: int(x, 16), required=True)
    args = parser.parse_args()
    packages = Packages(args.game_root, args.pid)
    cls, blob = packages.read(args.scenario)
    if cls != 0x80809994:
        raise ValueError('Bad scenario class')
    bc, bs, _ = array(blob, 80, 0x8080924D, 24)
    all_rows = []
    for bi in range(bc):
        sc, ss, _ = array(blob, bs + bi * 24 + 8, 0x8080924F, 76)
        for si in range(sc):
            entry_tag = u32(blob, ss + si * 76 + 68)
            _, entry = packages.read(entry_tag)
            registry_tag = u32(entry, 20)
            for object_tag in registry_objects(packages, registry_tag):
                all_rows.extend(slots_of(packages, object_tag))
    keys = Counter(row['key'] for row in all_rows)
    print('roster keys', {hex(k): v for k, v in keys.items()})
    print('types', dict(sorted(Counter(row['type'] for row in all_rows).items())))
    for row in all_rows:
        name = row.get('name')
        if name and ('mode_' in name or 'pvp_controller' in name or 'spawn' in name
                     or 'objective' in name or 'score' in name or 'team' in name):
            print(f"type={row['type']:>2} slot={row['index']:>3} key={row['key']:08x} "
                  f"component={row.get('component', 0):08x} "
                  f"sense={row.get('sense', 0):08x} auth={row.get('auth', 0):08x} "
                  f"tag={row.get('tag', 0):08x} "
                  f"name={name}")
    print('unresolved', sum('name' not in row for row in all_rows), '/', len(all_rows))


if __name__ == '__main__':
    main()
