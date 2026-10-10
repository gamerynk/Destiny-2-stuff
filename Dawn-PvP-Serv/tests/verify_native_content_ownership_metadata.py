from pathlib import Path
import hashlib, json, re, struct, sys
captured=(Path(sys.argv[1])/'native-text.bin').read_bytes()
text=bytearray(captured)
assert text[0xD4C550-0x1000] == 0xE9
text[0xD4C550-0x1000:0xD4C555-0x1000]=bytes.fromhex('4c8bdc5556')
patterns={
 'fetch': '48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 41 0F B6 F8 48 8B DA 48 8B F1 E8 ? ? ? ? BA 19 00 00 00 B9 48 F8 14 00',
 'parse': '40 55 48 8D AC 24 50 F4 FF FF 48 81 EC B0 0C 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 A0 0B 00 00 83 3D ? ? ? ? 00 0F 85',
 'state': '8B 05 ? ? ? ? C3 ? ? ? ? ? ? ? ? ? 48 83 EC 28 B8 FF FF 00 00 C7 05 ? ? ? ? 01 00 00 00 B9 18 00 00 00 66 89 05',
 'tick': '4C 8B DC 55 56 57 49 8D AB 88 F9 FF FF 48 81 EC 60 07 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 50 06 00 00 48 8B F9 49 89 5B 10'}
hits={}
for name,pattern in patterns.items():
 regex=b''.join(b'.' if x=='?' else re.escape(bytes([int(x,16)])) for x in pattern.split())
 matches=[x.start()+0x1000 for x in re.finditer(regex,text,re.S)]
 assert len(matches)==1,(name,matches)
 hits[name]=matches[0]
def relative(rva,offset,end):
 return rva+end+struct.unpack_from('<i',text,rva-0x1000+offset)[0]
state=[relative(hits['state'],2,6),relative(hits['parse'],36,41),relative(hits['fetch'],54,62)]
assert state==[0x1F916FC]*3
gate=relative(hits['parse'],0xB9,0xBE)
assert gate==0x1F91698
assert text[hits['fetch']-0x1000+0x9A:hits['fetch']-0x1000+0xAA]==bytes.fromhex('488b5c2450488b7424584883c4405fc3')
assert text[hits['parse']-0x1000+0x571:hits['parse']-0x1000+0x57A]==bytes.fromhex('4881c4b00c00005dc3')
assert text[hits['fetch']-0x1000+52:hits['fetch']-0x1000+54]==bytes.fromhex('c705')
assert text[hits['parse']-0x1000+0xB7:hits['parse']-0x1000+0xB9]==bytes.fromhex('803d')
assert relative(hits['tick'],0x1E4,0x1E8)==hits['fetch']
assert text[hits['tick']-0x1000+0x6A1:hits['tick']-0x1000+0x6AC]==bytes.fromhex('4881c4600700005f5e5dc3')
assert relative(hits['tick'],0x71C,0x720)==hits['tick']+0x341
report={'native_text_sha256':hashlib.sha256(captured).hexdigest(),'target_mutations':0,
 'offline_tick_prologue_restoration':'first5 bytes4c8bdc5556 from baseline native discovery signature; no live change',
 'unique_signature_hits':{k:hex(v)for k,v in hits.items()},
 'state_getter_parser_fetch_RIP_targets':[hex(x)for x in state],
 'manifest_gate_RIP_target':hex(gate),
 'complete_fetch_range':[hex(hits['fetch']),hex(hits['fetch']+0xAA)],
 'complete_parser_range':[hex(hits['parse']),hex(hits['parse']+0x57A)],
 'complete_tick_range_including_cold_branches':[hex(hits['tick']),hex(hits['tick']+0x720)],
 'validated_epilogues':True,
 'state_interpretation':'Fetch publishes 0 before GET; parser only parses state0 and publishes nonzero before cleanup; reset/init publish -1.',
 'limitations':'Historical capture only. Native x64 stack walk fails closed on unsupported VM/unwind frames.'}
Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
print('PASS: four unique native ownership signatures, state/gate targets, fetch/parser/tick ranges and cold tick continuation')
