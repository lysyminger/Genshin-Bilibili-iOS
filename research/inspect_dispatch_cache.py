"""Inspect only dispatch metadata in the target app cache; raw cache stays in memory."""
import os
import argparse
import asyncio
import base64
import concurrent.futures
from pathlib import Path
import json
import sqlite3
import struct
import sys
from urllib.parse import parse_qsl, urlsplit
import requests

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / '.tools/pymobiledevice3'
sys.path[:0] = [str(BASE), str(BASE / 'win32'), str(BASE / 'win32/lib')]
from pymobiledevice3.usbmux import list_devices
from pymobiledevice3.lockdown import create_using_usbmux
from pymobiledevice3.services.house_arrest import HouseArrestService
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import padding

KEYS = ROOT / 'private/dispatch-decode'
PRIVATE = serialization.load_pem_private_key((KEYS / 'ClientPri4.pem').read_bytes(), None)
PUBLIC = serialization.load_pem_public_key((KEYS / 'ServerPub.pem').read_bytes())
BUNDLE_ID = os.environ.get('GB_IOS_BUNDLE_ID', 'com.miHoYo.Yuanshen')
APP_CACHE = '/Library/Caches/' + BUNDLE_ID

def varint(data, offset):
    value = shift = 0
    while offset < len(data) and shift <= 70:
        byte = data[offset]; offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
        shift += 7
    raise ValueError('invalid varint')

def summarize(body):
    obj = json.loads(body)
    encrypted = base64.b64decode(obj['content'], validate=True)
    raw = b''.join(PRIVATE.decrypt(encrypted[i:i+256], padding.PKCS1v15())
                   for i in range(0, len(encrypted), 256))
    result = {'encrypted_bytes': len(encrypted), 'decrypted_bytes': len(raw)}
    try:
        PUBLIC.verify(base64.b64decode(obj['sign']), raw, padding.PKCS1v15(), hashes.SHA256())
        result['reference_signature_valid'] = True
    except Exception:
        result['reference_signature_valid'] = False
    offset = 0
    while offset < len(raw):
        tag, offset = varint(raw, offset); field, wire = tag >> 3, tag & 7
        if wire == 0:
            value, offset = varint(raw, offset)
            if field == 1:
                result['retcode'] = value if value < 2**63 else value - 2**64
        elif wire == 2:
            size, offset = varint(raw, offset)
            value = raw[offset:offset+size]; offset += size
            if offset > len(raw):
                raise ValueError('invalid protobuf length')
            if field == 2:
                message = value.decode('utf-8', errors='replace').lower()
                result['message_class'] = next((s for s in ('not match region', 'not found version config')
                                                 if s in message), 'other' if message else 'empty')
            elif field == 3:
                result['region_info_present'] = True
        elif wire in (1, 5):
            offset += {1: 8, 5: 4}[wire]
        else:
            raise ValueError('invalid protobuf wire')
    result.setdefault('retcode', 0)
    result['asset_platform_markers'] = [s for s in ('Android', 'iOS', 'StandaloneWindows') if s.encode() in raw]
    return result

def snapshot_database(database, wal):
    raw = bytearray(database)
    assert raw.startswith(b'SQLite format 3\0') and len(wal) >= 32
    page_size = struct.unpack_from('>I', wal, 8)[0]
    assert 512 <= page_size <= 65536
    frames = []; committed = pages = 0
    for offset in range(32, len(wal)-24-page_size+1, 24+page_size):
        number, size = struct.unpack_from('>II', wal, offset)
        if wal[offset+8:offset+16] != wal[16:24] or not number:
            break
        frames.append((number, wal[offset+24:offset+24+page_size]))
        if size:
            committed = len(frames); pages = size
    assert 0 < pages*page_size <= 25000000
    raw.extend(b'\0' * max(0, pages*page_size-len(raw)))
    for number, page in frames[:committed]:
        assert number <= pages
        raw[(number-1)*page_size:number*page_size] = page
    del raw[pages*page_size:]
    raw[18] = raw[19] = 1  # Deserialize a committed snapshot without an external WAL file.
    connection = sqlite3.connect(':memory:')
    connection.deserialize(bytes(raw))
    assert connection.execute('PRAGMA quick_check').fetchall() == [('ok',)]
    return connection

async def read_entries():
    devices = [d for d in await list_devices() if d.connection_type == 'USB']
    assert len(devices) == 1
    lockdown = await create_using_usbmux(serial=devices[0].serial, autopair=False, connection_type='USB')
    try:
        async with await HouseArrestService.create(lockdown, BUNDLE_ID) as afc:
            database = await afc.get_file_contents(APP_CACHE + '/Cache.db')
            wal = await afc.get_file_contents(APP_CACHE + '/Cache.db-wal')
            connection = snapshot_database(database, wal)
            rows = connection.execute("SELECT r.entry_ID,r.request_key,d.isDataOnFS,d.receiver_data "
                "FROM cfurl_cache_response r JOIN cfurl_cache_receiver_data d ON r.entry_ID=d.entry_ID "
                "WHERE r.request_key LIKE '%cnqddispatch.yuanshen.com/query_cur_region%' ORDER BY r.entry_ID").fetchall()
            connection.close()
            result = []
            for entry, request_key, on_fs, body in rows:
                # Cache keys prepend a small method/cache discriminator to the URL.
                start = request_key.find('https://')
                url = urlsplit(request_key[start:])
                assert start >= 0 and url.hostname == 'cnqddispatch.yuanshen.com' and url.path == '/query_cur_region'
                if on_fs:
                    name = body.decode('ascii')
                    assert name and all(c in '0123456789abcdefABCDEF-' for c in name)
                    body = await afc.get_file_contents(APP_CACHE + '/fsCachedData/' + name)
                result.append((entry, dict(parse_qsl(url.query)), body))
            return result
    finally:
        await lockdown.close()

def compare(parameters):
    cases = [('iOS', 1), ('iOS', 2), ('Android', 1), ('Android', 2), ('Win', 1), ('Win', 3)]
    def probe(case):
        version, platform = case
        params = dict(parameters, version='CNREL'+version+'7.1.0', platform=str(platform))
        response = requests.get('https://cnqddispatch.yuanshen.com/query_cur_region', params=params,
                                timeout=15, allow_redirects=False)
        row = {'version': params['version'], 'platform': platform, 'http': response.status_code}
        try:
            row.update(summarize(response.content))
        except Exception as error:
            row['parse_error'] = type(error).__name__
        return row
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        return list(pool.map(probe, cases))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compare', action='store_true', help='Read-only metadata probes to the same official B dispatch; credentials are never printed.')
    args = parser.parse_args()
    entries = asyncio.run(asyncio.wait_for(read_entries(), timeout=25))
    report = {'cached_responses': [dict(cache_entry=entry, **summarize(body)) for entry, _, body in entries]}
    if args.compare and entries:
        report['metadata_comparison'] = compare(entries[-1][1])
    path = ROOT / 'private/device-logs/v0.14-dispatch-cache-analysis.json'
    path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
