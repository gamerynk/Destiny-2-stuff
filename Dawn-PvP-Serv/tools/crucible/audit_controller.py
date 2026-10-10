import argparse
from collections import Counter
import json
from pathlib import Path

from inspect_modes import registry_objects, slots_of
from package_reader import Packages, array, u32


SCENARIO_CLASS = 0x80809994
BRANCHES = (
    'placements_capture_zones', 'mode_breakthrough', 'mode_demolition',
    'mode_rumble', 'survival_team', 'mode_supremacy', 'm_directive_sensor_team_',
)
SHARED_BRANCHES = tuple(branch for branch in BRANCHES
                        if branch != 'survival_team')


def controller_rows(packages, scenario_tag):
    cls, blob = packages.read(scenario_tag)
    if cls != SCENARIO_CLASS:
        raise ValueError(f'{scenario_tag:#x} has class {cls:#x}')
    bubble_count, bubble_start, _ = array(blob, 80, 0x8080924D, 24)
    rows = []
    for bubble in range(bubble_count):
        state_count, state_start, _ = array(blob, bubble_start + bubble * 24 + 8,
                                            0x8080924F, 76)
        for state in range(state_count):
            entry_tag = u32(blob, state_start + state * 76 + 68)
            _, entry = packages.read(entry_tag)
            for object_tag in registry_objects(packages, u32(entry, 20)):
                rows.extend(slots_of(packages, object_tag))
    controller_keys = {row['key'] for row in rows
                       if row.get('name') == 'pvp_controller.pvp_music'}
    if len(controller_keys) != 1:
        raise ValueError(f'{scenario_tag:#x}: expected one controller key, got '
                         f'{[hex(key) for key in controller_keys]}')
    return [row for row in rows if row['key'] in controller_keys]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--catalog', type=Path,
                        default=Path('docs/crucible/native-maps.json'))
    parser.add_argument('--check', action='store_true',
                        help='verify checked catalog counts and shared branches')
    parser.add_argument('--slot-signatures', action='store_true',
                        help='show distinct fixed-slot identities across maps')
    args = parser.parse_args()
    maps = json.loads(args.catalog.read_text())['maps']
    packages = Packages(args.game_root, args.pid)
    totals = Counter()
    branch_coverage = Counter()
    signatures = {}
    for item in maps:
        rows = controller_rows(packages, item['scenario_tag'])
        names = [row.get('name', '') or '' for row in rows]
        types = Counter(row['type'] for row in rows)
        if args.slot_signatures:
            for row in rows:
                if row['index'] <= 121:
                    signature = (row['type'], row.get('name'),
                                 row.get('component'), row.get('auth'))
                    signatures.setdefault(row['index'], set()).add(signature)
        branches = [branch for branch in BRANCHES
                    if any(branch in name for name in names)]
        branch_coverage.update(branches)
        totals.update(types)
        print(f"{item['name']}: slots={len(rows)} type4={types[4]} "
              f"type26={types[26]} type32={types[32]} type34={types[34]} "
              f"type49={types[49]} type50={types[50]} type68={types[68]} "
              f"branches={','.join(branches)}")
    print('controller types', dict(sorted(totals.items())))
    print('branch map coverage', dict(sorted(branch_coverage.items())))
    if args.slot_signatures:
        for slot, variants in sorted(signatures.items()):
            if slot in (13, 14, 15, 27, 28, 29, 30, 31, 32, 33, 34, 35,
                        49, 50, 60, 61, 62, 63, 64, 65, 66, 67, 68,
                        69, 70, 75, 76):
                print(f'fixed_slot={slot} variants={len(variants)} '
                      f'signatures={sorted(variants, key=repr)}')
    if args.check and (len(maps) != 31 or totals[49] != 1254
                       or totals[50] != 2166
                       or branch_coverage['survival_team'] != 12
                       or any(branch_coverage[branch] != len(maps)
                              for branch in SHARED_BRANCHES)):
        raise SystemExit('Controller roster differs from the checked v38 catalog')


if __name__ == '__main__':
    main()
