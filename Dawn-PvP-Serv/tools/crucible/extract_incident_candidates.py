from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

from localized_strings import Strings
from package_reader import Packages, array, u32


INCIDENT_TABLE = 0x80B9E5BF
PRESENTATION_TABLE = 0x80B9E5E3
PVP_PRESENTATIONS = {
    'Defeated', 'Eliminated', 'Crest Created', 'Zone Captured',
    'Zone A Captured', 'Zone B Captured', 'Zone C Captured',
    'Zone A Lost', 'Zone B Lost', 'Zone C Lost', 'Zone Advantage',
    'Team Captured the Zone', 'Opponents Captured the Zone',
    'Round Won', 'Round Win', 'Opponents Eliminated',
    'Charge Set', 'Charge Defused', 'Charge Detonated',
    'Vault Hacked', 'Vault Defended', 'Breaker deployed',
    'Crests secured', 'Crests recovered',
}


def extract(packages: Packages) -> dict:
    incident_class, incident = packages.read(INCIDENT_TABLE)
    presentation_class, presentation = packages.read(PRESENTATION_TABLE)
    if incident_class != 0x80807C9B or presentation_class != 0x80806485:
        raise ValueError('Installed incident or presentation class changed')
    incident_count, incident_start, _ = array(incident, 0x18, 0x80808782, 40)
    list_count, list_start, _ = array(incident, 0x38, 0x80808788, 16)
    presentation_count, presentation_start, _ = array(
        presentation, 0x28, 0x80806489, 24)
    if incident_count != 7763:
        raise ValueError('Native incident target count changed')
    strings = Strings(packages)
    labels_by_presentation = {}
    for index in range(presentation_count):
        row = presentation_start + index * 24
        variants, first, _ = array(presentation, row + 8, 0x8080648B, 0x90)
        labels = set()
        for variant in range(variants):
            entry = first + variant * 0x90
            key, bank = u32(presentation, entry + 8), u32(presentation, entry + 24)
            try:
                label = strings.bank(bank).get(key, '').strip()
            except (KeyError, IndexError, ValueError):
                continue
            if label in PVP_PRESENTATIONS:
                labels.add(label)
        if labels:
            labels_by_presentation[u32(presentation, row)] = sorted(labels)
    records = []
    for target in range(incident_count):
        row = incident_start + target * 40
        list_index = struct.unpack_from('<h', incident, row + 16)[0]
        if not 0 <= list_index < list_count:
            continue
        count, first, _ = array(incident, list_start + list_index * 16,
                                0x8080878A, 4)
        labels = set()
        for ordinal in range(count):
            labels.update(labels_by_presentation.get(u32(incident, first + ordinal * 4), []))
        if labels:
            payload = row + 8 + struct.unpack_from('<q', incident, row + 8)[0]
            records.append({
                'target_index': target,
                'incident_hash': f'0x{u32(incident, row):08X}',
                'native_type': u32(incident, row + 36),
                'payload_class': f'0x{u32(incident, payload - 4):08X}',
                'labels': sorted(labels),
            })
    expected = {6089: 'Crest Created', 553: 'Zone Captured',
                2212: 'Round Won', 3281: 'Zone Advantage'}
    by_target = {row['target_index']: row for row in records}
    for target, label in expected.items():
        if label not in by_target.get(target, {}).get('labels', []):
            raise ValueError(f'Installed incident presentation changed at target {target}')
    return {
        'source': 'Installed class 0x80807C9B incident table and 0x80806485 presentations',
        'incident_table': f'0x{INCIDENT_TABLE:08X}',
        'presentation_table': f'0x{PRESENTATION_TABLE:08X}',
        'incident_target_count': incident_count,
        'candidate_count': len(records),
        'candidates': records,
        'limitation': ('These rows identify presentation strings and payload classes. '
                       'Their delivery direction, event authority, actor IDs, and score rules '
                       'need a native trace before gameplay use.'),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--image', type=Path)
    source.add_argument('--pid', type=int)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    report = extract(Packages(args.game_root, args.pid if args.pid is not None else args.image))
    content = json.dumps(report, indent=2) + '\n'
    if args.check:
        if not args.output.exists() or args.output.read_text() != content:
            raise SystemExit(f'Generated file is stale: {args.output}')
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(content)
    print(f"{report['candidate_count']} presentation-linked incident targets")


if __name__ == '__main__':
    main()
