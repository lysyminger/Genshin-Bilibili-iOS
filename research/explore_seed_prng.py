"""Broader search: which common transform maps local fingerprint data to the stored fp/dispatchSeed?"""
from pathlib import Path
import json
_local_values = json.loads((Path(__file__).resolve().parents[1] / 'private/seed_research.json').read_text(encoding='utf-8'))
import hashlib
import random
import struct

SEED_TARGET = _local_values['explore_seed_prng.py:SEED_TARGET']
FP_TARGET = _local_values['explore_seed_prng.py:FP_TARGET']
SEED_ID = _local_values['explore_seed_prng.py:SEED_ID']
SEED_TIME = _local_values['explore_seed_prng.py:SEED_TIME']
FP = _local_values['explore_seed_prng.py:FP']
FILE = open('private/android-prefs/combo_device_fingerprint.xml', 'rb').read()
DEVICE = open('private/android-prefs/device_data.xml', 'rb').read()
JSON = '{"seedId":"' + SEED_ID + '","seedTime":"' + SEED_TIME + '"}'
JSON_ESC = '{&quot;seedId&quot;:&quot;' + SEED_ID + '&quot;,&quot;seedTime&quot;:&quot;' + SEED_TIME + '&quot;}'
VERSIONS = ['CNRELAndroid7.1.0', 'CNRELiOS7.1.0', '7.1.0', '7.1.0_48052158_48145775', '1241', '48052158', '48145775']
PREFIXES = ['', 'seed', 'seed_', 'Seed', 'dispatch', 'dispatch_', 'dispatchSeed', 'combo', 'fp', 'device', 'device_id', 'aid', 'uid']

atoms = {
    'id': SEED_ID, 'idhex': SEED_ID.replace('-', ''), 'idupper': SEED_ID.upper(),
    'time': SEED_TIME, 'timehex': format(int(SEED_TIME), 'x'),
    'fp': FP, 'json': JSON, 'json_esc': JSON_ESC,
    'file': FILE, 'device': DEVICE,
}
texts = dict(atoms)
for p in PREFIXES:
    for a, av in atoms.items():
        if isinstance(av, bytes):
            continue
        texts[p + a] = p + av
        for v in VERSIONS:
            texts[p + a + v] = p + av + v
            texts[p + v + a] = p + v + av
            texts[p + a + ':' + v] = p + av + ':' + v
for v in VERSIONS:
    for p in PREFIXES:
        texts[p + 'v' + v] = p + v

found = []


def check(kind, name, digest, targets):
    if isinstance(digest, str):
        hexed = digest.lower()
    else:
        hexed = digest.hex()
    for tname, target in targets:
        variants = {
            'first': hexed[:len(target)],
            'last': hexed[-len(target):],
            'swap_first': ''.join(reversed([hexed[i:i + 2] for i in range(0, len(target), 2)]))[:len(target)],
            'xor_halves': None,
        }
        for variant, value in variants.items():
            if value and value == target:
                found.append((kind, name, tname, variant, hexed[:32]))
                print('MATCH', kind, name, tname, variant, hexed[:32])


targets = [('seed', SEED_TARGET), ('fp', FP_TARGET)]
for name, value in texts.items():
    data = value if isinstance(value, bytes) else value.encode()
    for algo in ('md5', 'sha1', 'sha256', 'sha512', 'sha3_256', 'blake2b', 'blake2s'):
        try:
            check(algo, name, hashlib.new(algo, data).digest(), targets)
        except ValueError:
            pass
    # FNV-1a 64 / FNV-1 64 / CRC32 over width
    h = 0xcbf29ce484222325
    for byte in data:
        h = ((h ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    check('fnv1a64', name, format(h, '016x'), targets)
    h = 0xcbf29ce484222325
    for byte in data:
        h = ((h * 0x100000001b3) & ((1 << 64) - 1)) ^ byte
    check('fnv1_64', name, format(h, '016x'), targets)

# Python MT19937 with seed from seed time and from id hash
for seed in (int(SEED_TIME), int(SEED_TIME) & 0xFFFFFFFF, 0):
    rng = random.Random(seed)
    for draw in range(6):
        check('python-mt', f'getrandbits64 seed={seed} draw={draw}', format(rng.getrandbits(64), '016x'), targets)


def splitmix64(seed):
    seed = (seed + 0x9E3779B97F4A7C15) & ((1 << 64) - 1)
    z = seed
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & ((1 << 64) - 1)
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & ((1 << 64) - 1)
    return (z ^ (z >> 31)) & ((1 << 64) - 1)


for seed in (int(SEED_TIME), int(SEED_TIME) & 0xFFFFFFFF, 0):
    value = seed
    for draw in range(3):
        check('splitmix64', f'seed={seed} draw={draw}', format(splitmix64(value), '016x'), targets)
        value = splitmix64(value)


class DotNetRandom:
    def __init__(self, seed):
        self.seed = [0] * 56
        subtraction = 0x80000000 if seed == -0x80000000 else abs(seed)
        mj = (161803398 - subtraction) & 0x7FFFFFFF
        self.seed[55] = mj
        mk = 1
        for i in range(1, 55):
            ii = (21 * i) % 55
            self.seed[ii] = mk
            mk = (mj - mk) & 0x7FFFFFFF
            if mk < 0:
                mk += 0x7FFFFFFF
            mj = self.seed[ii]
        for _ in range(4):
            for i in range(1, 56):
                self.seed[i] = (self.seed[i] - self.seed[1 + (i + 30) % 55]) & 0x7FFFFFFF
                if self.seed[i] < 0:
                    self.seed[i] += 0x7FFFFFFF
        self.inext = 0
        self.inextp = 21
        for _ in range(4):
            self.internal_sample()

    def internal_sample(self):
        loc_inext = self.inext + 1
        if loc_inext >= 56:
            loc_inext = 1
        loc_inextp = self.inextp + 1
        if loc_inextp >= 56:
            loc_inextp = 1
        value = (self.seed[loc_inext] - self.seed[loc_inextp]) & 0x7FFFFFFF
        if value == 0x7FFFFFFF:
            value -= 1
        self.seed[loc_inext] = value
        self.inext = loc_inext
        self.inextp = loc_inextp
        return value

    def next_int(self):
        return self.internal_sample()


for seed in (int(SEED_TIME) & 0x7FFFFFFF, int(SEED_TIME) & 0xFFFFFFFF, 0, 1):
    rng = DotNetRandom(seed)
    for draw in range(6):
        hi = rng.next_int()
        lo = rng.next_int()
        value = ((hi << 32) | lo) & ((1 << 64) - 1)
        check('dotnet-legacy', f'seed={seed} draw={draw}', format(value, '016x'), targets)
        check('dotnet-legacy', f'seed={seed} reversed draw={draw}', format((lo << 32) | hi, '016x'), targets)

print('searched candidates:', len(texts), 'found:', len(found))
