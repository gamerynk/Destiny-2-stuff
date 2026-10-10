"""Record installed Crucible activity policies and proven symbolic mode hashes; rule words are not yet understood"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

from package_reader import Packages, array, u32


PUBLIC_TABLE = 0x81327CF0
PUBLIC_TABLE_CLASS = 0x808076F0
PUBLIC_ROW_CLASS = 0x808076FC
POLICY_CLASS = 0x80807740
OPTION_CLASS = 0x80807D82
FIRST_POLICY_ACTIVITY = 483
AFTER_LAST_POLICY_ACTIVITY = 536

# names are listed only when their FNV-1 hash matches the symbolic hash in record +0xDC
SYMBOLIC_NAMES = (
    'pvp_control', 'pvp_clash', 'pvp_rumble', 'pvp_survival',
    'pvp_countdown', 'pvp_supremacy', 'pvp_breakthrough', 'pvp_mayhem',
    'pvp_lockdown', 'pvp_showdown', 'pvp_scorched', 'pvp_elimination',
    'pvp_doubles', 'pvp_momentum', 'pvp_control_iron_banner',
    'pvp_doubles_crimson', 'pvp_scorched_team',
    'pvp_demolition',
    'pvp_hardware',
)


def fnv1(name: str) -> int:
    value = 0x811C9DC5
    for byte in name.encode('ascii'):
        value = ((value * 0x01000193) & 0xFFFFFFFF) ^ byte
    return value


def activity_record(data: bytes, rows: int, index: int) -> int:
    row = rows + index * 16
    return row + 8 + struct.unpack_from('<q', data, row + 8)[0]


def extract(packages: Packages) -> dict:
    cls, public = packages.read(PUBLIC_TABLE)
    if cls != PUBLIC_TABLE_CLASS:
        raise ValueError(f'Unexpected public table class: {cls:#x}')
    count, start, _ = array(public, 8, PUBLIC_ROW_CLASS, 16)
    if count <= AFTER_LAST_POLICY_ACTIVITY:
        raise ValueError('Public activity table is shorter than the installed PvP section')
    policies = []
    definitions = {}
    names = {fnv1(name): name for name in SYMBOLIC_NAMES}
    for index in range(FIRST_POLICY_ACTIVITY, AFTER_LAST_POLICY_ACTIVITY):
        row = start + index * 16
        begin = activity_record(public, start, index)
        end = activity_record(public, start, index + 1)
        if not 0 <= begin < end <= len(public):
            raise ValueError(f'Invalid activity record at {index}')
        references = []
        for offset in range(begin, end - 3, 4):
            tag = u32(public, offset)
            if not 0x81320000 <= tag < 0x81322000:
                continue
            try:
                ref_class, body = packages.read(tag)
            except (KeyError, IndexError, ValueError):
                continue
            if ref_class in (POLICY_CLASS, OPTION_CLASS):
                item = {'tag': f'0x{tag:08X}', 'class': f'0x{ref_class:08X}',
                        'record_offset': offset - begin}
                if item not in references:
                    references.append(item)
                if ref_class == POLICY_CLASS and tag not in definitions:
                    if len(body) != 168:
                        raise ValueError(f'Unexpected policy size at {tag:#x}')
                    definitions[tag] = {
                        'tag': f'0x{tag:08X}',
                        'class': f'0x{ref_class:08X}',
                        'header_hashes': [f'0x{u32(body, offset):08X}' for offset in (12, 16)],
                        'numeric_words_20_to_128': [u32(body, offset)
                                                    for offset in range(20, 132, 4)],
                        'tail_words_132_to_164': [f'0x{u32(body, offset):08X}'
                                                  for offset in range(132, 168, 4)],
                    }
        policy_refs = [item for item in references if item['class'] == f'0x{POLICY_CLASS:08X}']
        if len(policy_refs) != 1:
            raise ValueError(f'Expected one native policy reference at activity {index}: {policy_refs}')
        symbolic_hash = u32(public, begin + 0xDC)
        policies.append({'index': index, 'hash': f'0x{u32(public, row):08X}',
                         'symbolic_hash': f'0x{symbolic_hash:08X}',
                         'symbolic_name': names.get(symbolic_hash),
                         'record_bytes': end - begin, 'references': references})
    expected_names = {483: 'pvp_control', 488: 'pvp_rumble', 497: 'pvp_clash',
                      500: 'pvp_hardware',
                      509: 'pvp_control_iron_banner'}
    for index, name in expected_names.items():
        if policies[index - FIRST_POLICY_ACTIVITY]['symbolic_name'] != name:
            raise ValueError(f'Installed symbolic mode hash changed at activity {index}')
    return {
        'source': 'Installed public activity table and referenced native policy records',
        'public_table': f'0x{PUBLIC_TABLE:08X}',
        'section': 'Unnamed activity records immediately following 62 named PvP map records',
        'activity_count': len(policies), 'distinct_policy_count': len(definitions),
        'activities': policies,
        'policies': [definitions[tag] for tag in sorted(definitions)],
        'limitation': 'Unknown symbolic hashes remain unnamed; numeric policy words have no unproven rule semantics.',
    }


def render_header(result: dict) -> str:
    activities = result['activities']
    lines = [
        '// Generated by tools/crucible/extract_activity_policies.py from installed v38 packages.',
        '#pragma once',
        '#include <array>',
        '#include <cstddef>',
        '#include <cstdint>',
        '#include <string_view>',
        '',
        'namespace dawn::state::activity::crucible {',
        'struct NativeMode final {',
        '    std::int16_t activityIndex;',
        '    std::uint32_t activityHash;',
        '    std::uint32_t symbolicHash;',
        '    std::uint32_t policyTag;',
        '    std::uint32_t optionsTag;',
        '    std::string_view symbolicName;',
        '};',
        f'inline constexpr std::array<NativeMode, {len(activities)}> kNativeModes{{{{',
    ]
    for activity in activities:
        refs = activity['references']
        policy = next(ref for ref in refs if ref['class'] == f'0x{POLICY_CLASS:08X}')
        options = sorted({ref['tag'] for ref in refs
                          if ref['class'] == f'0x{OPTION_CLASS:08X}'})
        if len(options) > 1:
            raise ValueError(f"Multiple options references at activity {activity['index']}")
        option_tag = options[0] if options else '0x00000000'
        name = activity['symbolic_name'] or ''
        lines.append(
            f"    {{{activity['index']}, {activity['hash']}U, "
            f"{activity['symbolic_hash']}U, {policy['tag']}U, "
            f'{option_tag}U, "{name}"}},'
        )
    lines.extend([
        '}};',
        '/** Exact activity-index lookup; a map index or unknown variant has no mode record. */',
        '[[nodiscard]] constexpr const NativeMode* find_native_mode(std::int16_t index) noexcept {',
        f'    if (index < {FIRST_POLICY_ACTIVITY} || index >= {AFTER_LAST_POLICY_ACTIVITY}) return nullptr;',
        f'    return &kNativeModes[static_cast<std::size_t>(index - {FIRST_POLICY_ACTIVITY})];',
        '}',
        '} // namespace dawn::state::activity::crucible',
        '',
    ])
    return '\n'.join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--image', type=Path)
    source.add_argument('--pid', type=int)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    result = extract(Packages(args.game_root, args.pid if args.pid is not None else args.image))
    outputs = {args.output: json.dumps(result, indent=2) + '\n',
               args.header: render_header(result)}
    for path, content in outputs.items():
        if args.check:
            if not path.exists() or path.read_text() != content:
                raise SystemExit(f'Generated file is stale: {path}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    print(f"{result['activity_count']} activity records, "
          f"{result['distinct_policy_count']} distinct native policy records")


if __name__ == '__main__':
    main()
