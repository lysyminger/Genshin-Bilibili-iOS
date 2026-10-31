"""Read iOS context once, then finish fixed metadata controls in memory."""
import os
import asyncio
import argparse
import base64
from datetime import datetime
import json
from pathlib import Path
import re
import sys
import subprocess
import time
from urllib.parse import urlsplit, urlunsplit

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'research'))
from inspect_dispatch_cache import read_entries, summarize, list_devices, requests, PRIVATE, padding, varint
from validate_android_dispatch import read_context

async def load_ios():
    deadline = time.monotonic() + 600
    while time.monotonic() < deadline:
        devices = [d for d in await list_devices() if d.connection_type == 'USB']
        if len(devices) == 1:
            rows = await read_entries()
            if rows:
                return rows[-1][1]
        await asyncio.sleep(3)
    raise RuntimeError('No iOS context during connection window')

def fields(raw):
    offset = 0
    while offset < len(raw):
        tag, offset = varint(raw, offset); number, wire = tag >> 3, tag & 7
        if not number:
            raise ValueError('protobuf field')
        if wire == 0:
            value, offset = varint(raw, offset)
        elif wire == 2:
            size, offset = varint(raw, offset); value = raw[offset:offset+size]; offset += size
            if offset > len(raw):
                raise ValueError('protobuf size')
        elif wire in (1, 5):
            size = {1: 8, 5: 4}[wire]; value = raw[offset:offset+size]; offset += size
        else:
            raise ValueError('protobuf wire')
        yield number, wire, value

def resource_profile(body):
    obj = json.loads(body)
    data = base64.b64decode(obj['content'])
    raw = b''.join(PRIVATE.decrypt(data[i:i+256], padding.PKCS1v15()) for i in range(0, len(data), 256))
    region = next(v for n, w, v in fields(raw) if n == 3 and w == 2)
    result = {'resource_urls': {}, 'versions': {}, 'hashes': {}, 'resource_version_config': {}}
    url_fields = {8: 'resource_url', 9: 'data_url', 12: 'resource_url_bak', 13: 'data_url_bak'}
    for number, wire, value in fields(region):
        if number in url_fields and wire == 2:
            url = urlsplit(value.decode('ascii'))
            host = url.hostname or ''
            if host.endswith('.yuanshen.com') or host.endswith('.mihoyo.com'):
                result['resource_urls'][url_fields[number]] = urlunsplit((url.scheme, host, url.path, '', ''))
        elif number in (14, 18) and wire == 0 and value < 10**12:
            result['versions'][str(number)] = value
        elif number in (19, 20) and wire == 2:
            text = value.decode('ascii', errors='replace')
            if re.fullmatch(r'[0-9a-fA-F]{32,64}', text):
                result['hashes'][str(number)] = text
        elif number == 22 and wire == 2:
            for n, w, v in fields(value):
                if n in (1, 4) and w == 0 and v < 10**13:
                    result['resource_version_config'][str(n)] = v
                elif n == 3 and w == 2:
                    text = v.decode('ascii', errors='replace')
                    if re.fullmatch(r'[0-9a-fA-F]{32,64}', text):
                        result['resource_version_config']['md5'] = text
    return result

def probe(name, host, params):
    response = requests.get('https://' + host + '/query_cur_region', params=params,
                            timeout=15, allow_redirects=False)
    row = {'name': name, 'host': host, 'http': response.status_code}
    try:
        row.update(summarize(response.content))
        if row.get('retcode') == 0 and row.get('region_info_present'):
            row['resource_profile'] = resource_profile(response.content)
    except Exception as error:
        row['parse_error'] = type(error).__name__
    return row

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--start-android-and-wait', action='store_true')
    args = parser.parse_args()
    report = {'private_context_persisted': False, 'world_entry_verified': False, 'cases': []}
    print('Waiting for one USB iPhone cache read; no game login required.', flush=True)
    try:
        ios = asyncio.run(asyncio.wait_for(load_ios(), timeout=620))
        print('iOS context loaded in memory; USB no longer required for this batch.', flush=True)
        cases = [('ios_native_bili', 'cnqddispatch.yuanshen.com', dict(ios)),
                 ('ios_official_anonymous', 'cngfdispatch.yuanshen.com',
                    dict(ios, channel_id='1', account_type='1', aid='0'))]
        try:
            if args.start_android_and_wait:
                adb = [os.environ.get('GB_ADB', 'adb'), '-s', os.environ.get('GB_ANDROID_SERIAL', 'emulator-5554')]
                pid = subprocess.run(adb + ['shell', 'pidof', 'com.miHoYo.ys.bilibili'], capture_output=True, text=True, timeout=10).stdout.strip()
                if not pid:
                    subprocess.run(adb + ['shell', 'am', 'start', '-n', 'com.miHoYo.ys.bilibili/com.miHoYo.GetMobileInfo.MainActivity'], capture_output=True, timeout=15)
                    print('Normal Android launch requested; no injection.', flush=True)
                deadline = time.monotonic() + 180
                while True:
                    try:
                        android = read_context(); break
                    except RuntimeError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(3)
            else:
                android = read_context()
            cases.append(('android_catalog_ios_platform', 'cnqddispatch.yuanshen.com',
                          dict(android, platform='1')))
        except Exception as error:
            report['android_context_error_type'] = type(error).__name__
        for index, (name, host, params) in enumerate(cases):
            if index:
                time.sleep(3)
            row = probe(name, host, params)
            report['cases'].append(row)
            print(json.dumps(row), flush=True)
            if row.get('http') in (403, 429):
                break
        del ios, cases
    except Exception as error:
        report['error_type'] = type(error).__name__
    output = ROOT / 'private/device-logs' / ('v0.15-profile-controls-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.json')
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Sanitized profile report: ' + str(output), flush=True)
