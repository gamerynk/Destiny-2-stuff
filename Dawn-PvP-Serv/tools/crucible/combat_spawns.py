import math
import re
import struct

from package_reader import array, u32


SPAWN_SLOT_TYPE = 50
SPAWN_COMPONENT_CLASS = 0x808094D1
TEAM_SLOT_TYPE = 49
TEAM_COMPONENT_CLASS = 0x808094D5
OBJECT_CLASS = 0x80809462
DESCRIPTOR_CLASS = 0x80809C36
INDIRECT_CLASS = 0x80809468
REDIRECT_CLASS = 0x80809B14


def _hash_name(value):
    result = 0x811C9DC5
    for byte in value:
        result = ((result * 0x01000193) ^ byte) & 0xffffffff
    return result


def _authored_name(blob, expected_hash):
    matches = [name.decode('ascii') for name in
               re.findall(rb'[A-Za-z_][A-Za-z_0-9.\[\]]{3,}', blob)
               if _hash_name(name) == expected_hash]
    if len(set(matches)) != 1:
        raise ValueError('Cannot verify authored spawn name against slot hash')
    return matches[0]


def _object_tags(packages, registry_tag):
    cls, data = packages.read(registry_tag)
    if cls != 0x8080925E:
        raise ValueError('Object registry class mismatch')
    for descriptor in (8, 24, 40):
        count, start, element_class = array(data, descriptor, 0x80809260, 4)
        for ordinal in range(count):
            yield u32(data, start + ordinal * 4)


def _placed_descriptors(packages, object_blob):
    count, start, _ = array(object_blob, 56, None, 24)
    seen = set()
    for bubble_ordinal in range(count):
        at = start + bubble_ordinal * 24
        bubble = struct.unpack_from('<i', object_blob, at)[0]
        handle_count, handles, _ = array(object_blob, at + 8, None, 4)
        pending = [u32(object_blob, handles + i * 4) for i in range(handle_count)]
        for tag in pending:
            if tag in seen:
                continue
            seen.add(tag)
            cls, data = packages.read(tag)
            if cls == INDIRECT_CLASS:
                child_count, children, _ = array(data, 16, None, 4)
                pending.extend(u32(data, children + i * 4) for i in range(child_count))
            elif cls == REDIRECT_CLASS:
                pending.append(u32(data, 12))
            elif cls == DESCRIPTOR_CLASS:
                yield bubble, tag, data


def _spawns_of_object(packages, object_tag, registry_tag, wanted_type, wanted_class):
    cls, blob = packages.read(object_tag)
    if cls != OBJECT_CLASS:
        return []
    key = u32(blob, 12)
    count, start, _ = array(blob, 32, None, 8)
    slots = [(u32(blob, start + i * 8), u32(blob, start + i * 8 + 4))
             for i in range(count)]
    wanted = {i for i, (slot_type, _) in enumerate(slots)
              if slot_type == wanted_type}
    if not wanted:
        return []
    found = {}
    for bubble, placed_tag, data in _placed_descriptors(packages, blob):
        for base in range(0, len(data) - 128 + 1, 4):
            if (u32(data, base) != placed_tag or u32(data, base + 8) != 0x70
                    or u32(data, base + 48) != key):
                continue
            slot_type, ordinal = struct.unpack_from('<HH', data, base + 52)
            if ordinal not in wanted or slot_type != wanted_type:
                continue
            component_class = u32(data, base + 4)
            if component_class != wanted_class:
                raise ValueError('Spawn slot has unexpected component class')
            rotation = struct.unpack_from('<4f', data, base + 96)
            position = struct.unpack_from('<3f', data, base + 112)
            if not all(math.isfinite(value) for value in (*rotation, *position)):
                raise ValueError('Non-finite authored combat spawn')
            if ordinal in found:
                if found[ordinal]['placed_tag'] != placed_tag:
                    raise ValueError('Ambiguous placed descriptor for spawn slot')
                continue
            name = _authored_name(data, slots[ordinal][1])
            mode = name.split('.')[1] if name.startswith('pvp_controller.mode_') else None
            if mode is None and name.startswith('pvp_controller.modes.mode_'):
                mode = name.split('.')[2]
            if mode is None and wanted_type == TEAM_SLOT_TYPE:
                prefix = name.split('_', 1)[0]
                if prefix in ('breakthrough', 'demolition', 'survival'):
                    mode = 'mode_' + prefix
            team_name = re.search(r'(?:^|[._])team_?([0-9]+)(?:_|$)', name)
            found[ordinal] = {
                'slot_index': ordinal, 'name_hash': slots[ordinal][1],
                'name': name,
                'mode': mode,
                'team': int(team_name.group(1)) if team_name else None,
                'position': position, 'rotation': rotation,
                'bubble_index': bubble, 'placed_tag': placed_tag,
                'descriptor_offset': base, 'object_tag': object_tag,
                'registry_tag': registry_tag, 'registry_key': key,
            }
    if wanted != found.keys():
        raise ValueError(f'Missing {len(wanted - found.keys())} authored spawn descriptors')
    return [found[i] for i in sorted(found)]


def _spawns(packages, scenario, slot_type, component_class):
    seen_registries = set()
    seen_objects = set()
    result = []
    for bubble in scenario['bubbles']:
        for state in bubble['states']:
            registry_tag = state['registry_tag']
            if registry_tag in seen_registries:
                continue
            seen_registries.add(registry_tag)
            for object_tag in _object_tags(packages, registry_tag):
                if object_tag in seen_objects:
                    continue
                seen_objects.add(object_tag)
                result.extend(_spawns_of_object(packages, object_tag, registry_tag,
                                                slot_type, component_class))
    return result


def combat_spawns(packages, scenario):
    return _spawns(packages, scenario, SPAWN_SLOT_TYPE, SPAWN_COMPONENT_CLASS)


def team_spawns(packages, scenario):
    return _spawns(packages, scenario, TEAM_SLOT_TYPE, TEAM_COMPONENT_CLASS)
