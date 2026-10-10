from pathlib import Path
import argparse, hashlib, json, re, struct, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'crucible'))
from package_reader import Packages, array, u32

def declarations(path):
    text=path.read_text().split('namespace dreaming_city {',1)[1].split('} // namespace dreaming_city',1)[0]
    slots={int(n):[(int(i),int(t),int(d,16)) for i,t,d in re.findall(
        r'\{(\d+),(\d+),0x[0-9A-F]+U,0x[0-9A-F]+U,0x[0-9A-F]+U,0x([0-9A-F]+)U\}',body)]
        for n,body in re.findall(r'kSlots(\d+)\{\{(.*?)\}\};',text,re.S)}
    bindings=text.split('kPopulations{{')[1].split('}};')[0]
    bound={(int(i),int(j)) for i,j in re.findall(r'\{(\d+),(\d+),',bindings)}
    result=[]
    for scenario,key,obj,bubble_hash,bubble,idx in re.findall(
        r'\{"dreaming_city_freeroam",0x([0-9A-F]+)U,0x([0-9A-F]+)U,0x([0-9A-F]+)U,0x([0-9A-F]+)U,(\d+),kSlots(\d+)\}',text):
        for slot,kind,descriptor in slots[int(idx)]:
            if kind==1:result.append(dict(registryIndex=int(idx),scenario=scenario,registry=key,object=obj,
                bubble=int(bubble),bubbleHash=bubble_hash,slot=slot,descriptor=f'{descriptor:08X}',
                policyBound=(int(idx),slot) in bound))
    return result

def package_metadata(packages,row):
    tag=int(row['descriptor'],16);pid,index=(tag-0x80800000)>>13,tag&8191
    path,entries,blocks=packages.table(pid)
    if index>=len(entries)//16:raise ValueError('tag index outside installed package')
    cls,_,placement=struct.unpack_from('<IIQ',entries,index*16)
    block,offset,size=placement&16383,((placement>>14)&16383)*16,placement>>28
    if block>=len(blocks)//48:raise ValueError('block index outside installed package')
    start,length,patch,flags=struct.unpack_from('<IIHH',blocks,block*48)
    return dict(package=path.name,packageId=pid,entryIndex=index,resourceClass=f'{cls:08X}',
        entryBytes=size,firstBlock=block,firstBlockEncrypted=bool(flags&2),firstBlockCompressed=bool(flags&1),patch=patch)

def decode(packages,row):
    cls,data=packages.read(int(row['descriptor'],16))
    if cls!=0x80809C36:raise ValueError('unexpected source resource class')
    definition=24+struct.unpack_from('<q',data,24)[0]
    if definition<4 or definition+0xB8>len(data) or u32(data,definition-4)!=0x8080948F:
        raise ValueError('source definition class or bounds')
    if struct.unpack_from('<IHH',data,definition+0x30)!=(int(row['registry'],16),1,row['slot']):
        raise ValueError('source ownership identity differs from declaration')
    count,start,_=array(data,definition+0xA8,0x80808356,104)
    if not 1<=count<=8:raise ValueError('category width')
    categories=[]
    for i in range(count):
        category=start+i*104;choices=[]
        for variant in range(6):
            n,first,_=array(data,category+8+variant*16,0x80808358,24)
            if n>128:raise ValueError('choice width')
            for j in range(n):
                choice=first+j*24;weight=u32(data,choice+12)
                if not weight:continue
                member=choice+struct.unpack_from('<q',data,choice)[0]
                if member<definition or member+4>len(data) or u32(data,member-4)!=0x808099D8:
                    raise ValueError('member identity or offset')
                choices.append(dict(variant=variant,choice=j,weight=weight,memberOffset=member-definition,entity=f'{u32(data,member):08X}'))
        categories.append(dict(category=i,key=f'{u32(data,category):08X}',choices=choices))
    refs={}
    for label,offset in [('primaryRule',0x98),('fallbackRule',0xA0)]:
        registry,kind,slot=struct.unpack_from('<IHH',data,definition+offset)
        refs[label]=dict(registry=f'{registry:08X}',type=kind,slot=slot)
    return dict(resourceSha256=hashlib.sha256(data).hexdigest(),categories=categories,**refs,
        nativeQuota=None,quotaStatus='requires native rule/tactical policy; not inferred from choices')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,default=Path('../reference/production/Dawn-production'))
    parser.add_argument('--game',type=Path,default=Path('C:/Dawn Server'))
    parser.add_argument('--image',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();catalog=args.reference/'Dawn/src/state/activity/coo/open_world_catalog.h'
    rows=declarations(catalog)
    pins=json.loads((args.reference/'tools/coo/open_world_member_catalog_pins.json').read_text())['sources']
    pinmap={(r['registry'],r['source'],r['resource']):r for r in pins if r['activity']=='dreaming_city_freeroam'}
    packages=Packages(args.game,args.image) if args.image else Packages.__new__(Packages)
    if not args.image:
        packages.paths={}
        for path in (args.game/'packages').glob('*.pkg'):
            match=re.fullmatch(r'(.+)_([0-9a-fA-F]{4})_(\d+)',path.stem)
            if match:
                pid,patch=int(match[2],16),int(match[3])
                if pid not in packages.paths or patch>packages.paths[pid][0]:packages.paths[pid]=(patch,path)
    for row in rows:
        row['installed']=package_metadata(packages,row)
        pin=pinmap.get((row['registry'],row['slot'],row['descriptor']))
        if pin:row['referenceMemberProof']=pin
        if args.image:row['decoded']=decode(packages,row)
        elif not pin:row['unresolved']='encrypted source bytes require validated decode material; no quota or persistent scheduling inferred'
    result=dict(catalogSha256=hashlib.sha256(catalog.read_bytes()).hexdigest(),declared=len(rows),
        bound=sum(r['policyBound'] for r in rows),unbound=sum(not r['policyBound'] for r in rows),sources=rows,
        limitation='Declaration membership is exact; unbound ownership scheduling/quota remains unproven. No destination enabled.')
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('declared','bound','unbound')}))

if __name__=='__main__':main()
