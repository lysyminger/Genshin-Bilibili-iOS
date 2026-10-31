"""Expanded brute force over local identifiers for the 16-hex dispatchSeed / 13-hex fp."""
from pathlib import Path
import json
_local_values = json.loads((Path(__file__).resolve().parents[1] / 'private/seed_research.json').read_text(encoding='utf-8'))
import hashlib
import itertools
import json

SEED_TARGET = _local_values['explore_seed_expanded.py:SEED_TARGET']
FP_TARGET = _local_values['explore_seed_expanded.py:FP_TARGET']

ATOMS = _local_values['explore_seed_expanded.py:ATOMS']
SEPARATORS = ['', ':', '|', ',', '_', '-', '.', '\n', '/']
PREFIXES = ['', 'seed', 'seed_', 'dispatch', 'dispatch_', 'dispatchSeed', 'combo', 'devicefp', 'DeviceFp', 'deviceFp']
FP_PREFIX = 'key_com.miHoYo.devicefp_'


def digest_variants(data):
    if isinstance(data, str):
        data = data.encode()
    values = []
    for algo in ('md5', 'sha1', 'sha256', 'sha512', 'sha3_256', 'blake2b', 'blake2s'):
        values.append((algo, hashlib.new(algo, data).hexdigest()))
    h = 0xcbf29ce484222325
    for byte in data:
        h = ((h ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    values.append(('fnv1a64', format(h, '016x')))
    h = 0xcbf29ce484222325
    for byte in data:
        h = ((h * 0x100000001b3) & ((1 << 64) - 1)) ^ byte
    values.append(('fnv1_64', format(h, '016x')))
    # CRC64 ECMA
    poly = 0x42F0E1EBA9EA3693
    crc = 0
    for byte in data:
        crc ^= byte << 56
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & ((1 << 64) - 1) if crc & (1 << 63) else (crc << 1) & ((1 << 64) - 1)
    values.append(('crc64', format(crc, '016x')))
    return values


def check(data, label, hits):
    for algo, hexed in digest_variants(data):
        for target, tname in ((SEED_TARGET, 'seed'), (FP_TARGET, 'fp')):
            size = len(target)
            for variant, value in (('first', hexed[:size]), ('last', hexed[-size:])):
                if value == target:
                    hits.append((label, algo, tname, variant))
                    print('MATCH', label, algo, tname, variant, hexed[:32])
        if len(hexed) >= 32:
            for variant, value in (('xor', format(int(hexed[:16], 16) ^ int(hexed[16:32], 16), '016x')),):
                if value == SEED_TARGET:
                    hits.append((label, algo, 'seed', variant))
                    print('MATCH', label, algo, 'seed', variant, hexed[:32])


hits = []
seen = set()


def emit(text, label):
    key = (text, label)
    if key in seen:
        return
    seen.add(key)
    check(text, label, hits)


# singles and prefixed singles
for name, value in ATOMS.items():
    emit(value, name)
    emit(FP_PREFIX + value, 'key:' + name)
    for prefix in PREFIXES:
        emit(prefix + value, prefix + ':' + name)
        emit(prefix + '=' + value, prefix + '=:' + name)
        emit(prefix + ':' + value, prefix + ':x:' + name)

# pairs and triples
names = list(ATOMS)
for a, b in itertools.product(names, repeat=2):
    for sep in SEPARATORS:
        emit(ATOMS[a] + sep + ATOMS[b], a + sep + b)
        for prefix in PREFIXES[1:]:
            emit(prefix + ATOMS[a] + sep + ATOMS[b], prefix + ':' + a + sep + b)
for a, b, c in itertools.product(names, repeat=3):
    if a == b == c:
        continue
    emit(ATOMS[a] + ':' + ATOMS[b] + ':' + ATOMS[c], a + ':' + b + ':' + c)
    emit(ATOMS[a] + ATOMS[b] + ATOMS[c], a + b + c)
    if len(seen) > 400000:
        break
    if len(seen) > 250000:
        break

# exact JSON shapes
for text in (
    json.dumps({'seedId': ATOMS['seedId'], 'seedTime': ATOMS['seedTime']}, separators=(',', ':')),
    json.dumps({'seedId': ATOMS['seedId'], 'seedTime': int(ATOMS['seedTime'])}, separators=(',', ':')),
    json.dumps({'seed_id': ATOMS['seedId'], 'seed_time': ATOMS['seedTime']}, separators=(',', ':')),
    f"seedId={ATOMS['seedId']}&seedTime={ATOMS['seedTime']}",
):
    emit(text, 'json')

print('unique candidates:', len(seen), 'hits:', len(hits))
print('seed target:', SEED_TARGET, 'fp target:', FP_TARGET)
