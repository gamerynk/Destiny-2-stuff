import json
from pathlib import Path
import sqlite3
import struct


class Reader:
    def __init__(self, data): self.data, self.offset = data, 0
    def read(self, fmt):
        value = struct.unpack_from('<' + fmt, self.data, self.offset)
        self.offset += struct.calcsize('<' + fmt)
        return value[0] if len(value) == 1 else value
    def skip(self, count):
        self.offset += count
        assert self.offset <= len(self.data)
    def item(self):
        _, definition, level, quantity, serial, flags = self.read('QIiiiI')
        policy, count = self.read('BI')
        assert policy <= 1 and count <= 12
        plugs = [f'{self.read("I"):08X}' if self.read('B') else None for _ in range(12)]
        assert level >= 0 and quantity >= 0
        return dict(definition=f'{definition:08X}', level=level, derivedPower=max(750,level*10) if level else 0,
                    quantity=quantity, socketPolicy=policy, plugs=plugs)


def prefix(data, codec):
    assert codec >= 3, 'only exact current flagged character layout'
    r = Reader(data); r.skip(8); rewards = r.read('I'); assert rewards <= 1000; r.skip(rewards*8)
    profile = r.read('I'); assert profile <= 701; r.skip(profile*20)
    count = r.read('I'); assert count <= 3
    characters = []
    slots = ['kinetic','energy','heavy','helmet','gauntlets','chest','legs','classItem','ghost','vehicle','ship','subclass','clanBanner','emblem','emote','finisher']
    for index in range(count):
        r.skip(8); selected = r.read('B'); r.skip(3); level = r.read('B'); r.skip(2+4+4+1+5)
        equipment = {name:r.item() for name in slots if r.read('B')}
        inventory_count = r.read('I'); assert inventory_count <= 200
        for _ in range(inventory_count): r.item()
        r.skip(4)  # int32 nextInventorySerial, before the identity-header flag
        if r.read('B'): r.skip(36)
        characters.append(dict(characterIndex=index, selected=bool(selected), characterLevel=level, equipment=equipment))
    return characters


if __name__ == '__main__':
    database = Path(__file__).resolve().parents[3] / 'local-server/Restoration/storage/restoration.db'
    connection = sqlite3.connect(database.as_uri() + '?mode=ro', uri=True)
    rows = connection.execute('SELECT revision,state_codec,state_blob FROM accounts ORDER BY updated_at DESC').fetchall()
    print(json.dumps([dict(accountIndex=i,revision=revision,codec=codec,characters=prefix(blob,codec))
                      for i,(revision,codec,blob) in enumerate(rows)], indent=2))
