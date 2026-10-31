"""Search candidate derivations of the Android dispatchSeed from local device-fingerprint data."""
from pathlib import Path
import json
_local_values = json.loads((Path(__file__).resolve().parents[1] / 'private/seed_research.json').read_text(encoding='utf-8'))
import hashlib
import struct

TARGET = _local_values['explore_seed_derivation.py:TARGET']
SEED_ID = _local_values['explore_seed_derivation.py:SEED_ID']
SEED_TIME = _local_values['explore_seed_derivation.py:SEED_TIME']
FP = _local_values['explore_seed_derivation.py:FP']
JSON_TEXT = '{"seedId":"' + SEED_ID + '","seedTime":"' + SEED_TIME + '"}'

atoms = {
    'id': SEED_ID,
    'idhex': SEED_ID.replace('-', ''),
    'time': SEED_TIME,
    'fp': FP,
    'json': JSON_TEXT,
    'time_hex': format(int(SEED_TIME), 'x'),
}

candidates = {}
for a, av in atoms.items():
    for b, bv in atoms.items():
        for sep in ('', ':', '|', ',', '_', '-', '.', '\n'):
            candidates[f'{a}{sep}{b}'] = av + sep + bv
for a in atoms:
    for b in atoms:
        for c in atoms:
            candidates[f'{a}|{b}|{c}'] = atoms[a] + '|' + atoms[b] + '|' + atoms[c]
candidates.update(atoms)


def slices(name, key, data):
    if isinstance(data, str):
        data = data.encode()
    for handler in ('md5', 'sha1', 'sha256', 'sha512', 'blake2b', 'blake2s'):
        digest = hashlib.new(handler, data).hexdigest()
        variants = {
            'first16': digest[:16],
            'last16': digest[-16:],
            'xor64': format(int(digest[:16], 16) ^ int(digest[16:32], 16), '016x') if len(digest) >= 32 else None,
            'xor64_last': format(int(digest[-32:-16], 16) ^ int(digest[-16:], 16), '016x') if len(digest) >= 32 else None,
        }
        for variant, value in variants.items():
            if value and value.lower() == TARGET:
                print('MATCH', handler, name, key, variant, repr(data[:100]))


for key, value in candidates.items():
    slices('text', key, value)

raw_uuid = bytes.fromhex(SEED_ID.replace('-', ''))
time_le = struct.pack('<Q', int(SEED_TIME))
time_be = struct.pack('>Q', int(SEED_TIME))
for key, value in {
    'uuid+time_le': raw_uuid + time_le,
    'time_le+uuid': time_le + raw_uuid,
    'uuid+time_be': raw_uuid + time_be,
    'fp+time_le': FP.encode() + time_le,
    'time_le+fp': time_le + FP.encode(),
}.items():
    slices('bytes', key, value)


class JavaRandom:
    def __init__(self, seed):
        self.seed = (seed ^ 0x5DEECE66D) & ((1 << 48) - 1)

    def next(self, bits):
        self.seed = (self.seed * 0x5DEECE66D + 0xB) & ((1 << 48) - 1)
        return self.seed >> (48 - bits)

    def next_long(self):
        return (self.next(32) << 32) + self.next(32)


for seed in (int(SEED_TIME), 0, 1):
    rng = JavaRandom(seed)
    for draw in range(8):
        if format(rng.next_long(), '016x') == TARGET:
            print('MATCH java nextLong seed', seed, 'draw', draw)

print('candidates tested:', len(candidates) * 6)
print('target:', TARGET)
