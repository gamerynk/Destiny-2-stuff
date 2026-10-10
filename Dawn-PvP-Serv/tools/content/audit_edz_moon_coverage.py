import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from audit_dreaming_city_sources import Packages, package_metadata


def audit(reference, game):
    folder = reference / 'tools/coo'
    pins_path = folder / 'open_world_member_catalog_pins.json'
    research_path = folder / 'lost_sector_edz_moon_native_research.json'
    pins = json.loads(pins_path.read_text())['sources']
    research = json.loads(research_path.read_text())
    assert 'not retail quotas' in research['purpose']
    packages = Packages.__new__(Packages)
    packages.paths = {}
    for path in (game / 'packages').glob('*.pkg'):
        match = re.fullmatch(r'(.+)_([0-9a-fA-F]{4})_(\d+)', path.stem)
        if match:
            pid, patch = int(match[2], 16), int(match[3])
            if pid not in packages.paths or patch > packages.paths[pid][0]:
                packages.paths[pid] = (patch, path)
    result = {'purpose': research['purpose'], 'evidenceSha256': {
        p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (pins_path, research_path)}, 'worlds': {}}
    for namespace, activity, alias in [('edz', 'edz_freeroam', 'edz_freeroam'), ('moon', 'luna_freeroam', 'moon_freeroam')]:
        rows = [r for r in pins if r['activity'] in (activity, alias)]
        identities = {(r['registry'], r['source'], r['resource']) for r in rows}
        assert len(identities) == len(rows), 'duplicate source identity'
        sectors = [s for s in research['sectors'] if s['namespace'] == namespace]
        native_sources = []
        for row in rows:
            assert row['resourceClass'] == '80809C36' and row['definitionClass'] == '8080948F'
            assert 1 <= row['categoryCount'] <= 8
            assert all(0 <= c['category'] < row['categoryCount'] and c['weight'] > 0 for c in row['choices'])
            native_sources.append({'registry': row['registry'], 'slot': row['source'], 'descriptor': row['resource'],
                'origin': row['origin'], 'categories': row['categoryCount'],
                'installed': package_metadata(packages, {'descriptor': row['resource']})})
        result['worlds'][namespace] = {'sources': len(rows), 'origins': dict(Counter(r['origin'] for r in rows)),
            'categoryWidths': dict(Counter(r['categoryCount'] for r in rows)),
            'lostSectorBubbles': sorted(s['bubble'] for s in sectors),
            'ordinaryCoverage': 'Only one bootstrap source pinned; complete ordinary declarations and scheduling unresolved',
            'quotaStatus': 'Lost Sector estimated_target is not a verified retail request quota',
            'nativeSources': native_sources}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=Path('../reference/production/Dawn-production'))
    parser.add_argument('--game', type=Path, default=Path('C:/Dawn Server'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.reference, args.game)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({world: {k: v for k, v in data.items() if k != 'nativeSources'} for world, data in result['worlds'].items()}, indent=2))
