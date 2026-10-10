import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from package_reader import Packages, array, u32
from combat_spawns import combat_spawns, team_spawns
from localized_strings import Strings


def stem_of(path):
    name=re.sub(r'_[0-9a-f]{4}_\d+\.pkg$', '',path.name.removeprefix('w64_'))
    return name.removesuffix('_activities')


def scenario(packages, strings, row):
    cls,data=packages.read(row['tag'])
    if cls!=0x80809994:raise ValueError('Scenario class mismatch')
    count,start,_=array(data,80,0x8080924d,24)
    bubbles=[]
    loaded={(row['tag']-0x80800000)>>13}
    for ordinal in range(count):
        at=start+ordinal*24
        states,state_start,_=array(data,at+8,0x8080924f,76)
        values=[]
        for index in range(states):
            ptr=state_start+index*76
            tag=u32(data,ptr+68)
            entry_cls,entry=packages.read(tag)
            if entry_cls!=0x8080925b:raise ValueError('Slice entry class mismatch')
            registry=u32(entry,20)
            loaded.add((tag-0x80800000)>>13)
            values.append({'ordinal':index,'slice_set':ordinal*8+index,
                'enabled':bool(data[ptr]),'public':bool(data[ptr+12]),
                'state_hash':u32(data,ptr+4),'map_bubble':u32(data,ptr+28),
                'entry_tag':tag,'entry_index':u32(entry,16),'registry_tag':registry})
        bubbles.append({'ordinal':ordinal,'name_hash':u32(data,at),'states':values})
    name_hash=u32(data,32)
    string_bank=u32(data,40)
    display_name=strings.bank(string_bank).get(name_hash)
    if not display_name:
        raise ValueError('Scenario display name is absent from its English string bank')
    return {'name':row['name'].removesuffix(':scenario_client'),
        'display_name':display_name,'name_hash':name_hash,'string_bank':string_bank,
        'scenario_tag':row['tag'],
        'package':row['package'],'bubbles':bubbles,'loaded_packages':sorted(loaded)}


def spawn_sets(packages, pids):
    components={}
    bindings={}
    for pid in pids:
        for tag in packages.tags(pid,0x808099d6):
            _,data=packages.read(tag)
            if len(data)>=220:
                resource=u32(data,216)
                components[tag]=resource
    for pid in pids:
        for tag in packages.tags(pid,0x80808a54):
            _,data=packages.read(tag)
            count,start,_=array(data,40,0x80808bb0,4)
            mask=[bit for bit in range(256) if data[8+bit//8] & (1<<(bit%8))]
            for i in range(count):
                component=u32(data,start+i*4)
                if component in components:
                    bindings.setdefault(components[component],[]).append({
                        'container_tag':tag,'component_tag':component,'map_bubbles':mask})
    result=[]
    for pid in pids:
        for tag in packages.tags(pid,0x80809162):
            _,data=packages.read(tag)
            count,start,_=array(data,8,0x80809164,48)
            points=[]
            for i in range(count):
                at=start+i*48
                rotation=struct.unpack_from('<4f',data,at)
                position=struct.unpack_from('<3f',data,at+16)
                if not all(math.isfinite(x) for x in (*rotation,*position)):
                    raise ValueError('Non-finite authored spawn')
                points.append({'ordinal':i,'name_hash':u32(data,at+32),
                    'position':position,'rotation':rotation})
            result.append({'tag':tag,'package_id':pid,'package':packages.paths[pid][1].name,
                'bindings':bindings.get(tag,[]),'points':points})
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root',type=Path,required=True)
    source=parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--image',type=Path)
    source.add_argument('--pid',type=int)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--check',action='store_true',
        help='Verify that the committed JSON matches the installed package extraction')
    args=parser.parse_args()
    packages=Packages(args.game_root,args.pid or args.image)
    strings=Strings(packages)
    names=packages.names()
    maps=[]
    for row in sorted(names.values(),key=lambda r:r['name']):
        if not row['name'].startswith('pvp_') or row['class']!=0x80809994:continue
        parsed=scenario(packages,strings,row)
        stem=stem_of(packages.paths[row['package_id']][1])
        pids=[pid for pid,(_,path) in packages.paths.items() if stem_of(path)==stem]
        parsed['spawn_sets']=spawn_sets(packages,pids)
        parsed['combat_spawns']=combat_spawns(packages,parsed)
        parsed['team_spawns']=team_spawns(packages,parsed)
        for bubble in parsed['bubbles']:
            for state in bubble['states']:
                state['spawn_sets']=[s['tag'] for s in parsed['spawn_sets']
                    if (s['package_id'] in parsed['loaded_packages'] or
                        '_activities_' not in s['package'])
                    and any(state['map_bubble'] in b['map_bubbles'] for b in s['bindings'])]
        maps.append(parsed)
        print(parsed['name'],parsed['display_name'],len(parsed['spawn_sets']),'arrival sets',
            len(parsed['combat_spawns']),'combat spawns',
            len(parsed['team_spawns']),'team spawns',flush=True)
    report={'format':3,'source':'Installed v38 package records; newest patch of each package family',
        'executable_sha256':hashlib.sha256((args.game_root/'destiny2.exe').read_bytes()).hexdigest(),
        'map_count':len(maps),'spawn_set_count':sum(len(m['spawn_sets']) for m in maps),
        'spawn_point_count':sum(len(s['points']) for m in maps for s in m['spawn_sets']),
        'combat_spawn_count':sum(len(m['combat_spawns']) for m in maps),
        'team_spawn_count':sum(len(m['team_spawns']) for m in maps),
        'maps':maps}
    serialized=json.dumps(report,indent=2)+'\n'
    if args.check:
        if not args.output.is_file() or args.output.read_text()!=serialized:
            raise SystemExit(f'Catalog differs from installed packages: {args.output}')
        print('Catalog matches installed packages:',args.output)
    else:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(serialized)
        print('Extracted',report['map_count'],'maps,',report['spawn_set_count'],
            'arrival sets,',report['spawn_point_count'],'arrival points,',
            report['combat_spawn_count'],'combat spawns,',
            report['team_spawn_count'],'team spawns')


if __name__=='__main__':main()
