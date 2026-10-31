"""Keep one diagnostic context in memory; accept only fixed metadata operations."""
import asyncio
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'research'))
from inspect_dispatch_cache import read_entries, summarize, requests, PRIVATE, padding
from inspect_v15_profile_controls import fields, resource_profile
from validate_android_dispatch import read_context

def metadata(name, host, params):
    response = requests.get('https://' + host + '/query_cur_region', params=params,
                            timeout=15, allow_redirects=False)
    row = {'name': name, 'http': response.status_code}
    row.update(summarize(response.content))
    fingerprints = {}
    if row.get('retcode') == 0 and row.get('region_info_present'):
        row['resource_profile'] = resource_profile(response.content)
        data = base64.b64decode(response.json()['content'])
        raw = b''.join(PRIVATE.decrypt(data[i:i+256], padding.PKCS1v15()) for i in range(0, len(data), 256))
        region = next(v for n, w, v in fields(raw) if n == 3 and w == 2)
        for n, w, v in fields(region):
            if n in (8, 9, 12, 13, 14, 18, 19, 20, 22, 26, 27, 34, 35):
                fingerprints[str(n)] = {'wire': w, 'sha256': hashlib.sha256(v if isinstance(v, bytes) else str(v).encode()).hexdigest()}
    return row, fingerprints

def main(restart_android=False):
    # Timeout destroys the local context even if the operator leaves stdin idle.
    timer = threading.Timer(1800, lambda: os._exit(0)); timer.daemon = True; timer.start()
    ios = asyncio.run(asyncio.wait_for(read_entries(), 25))[-1][1]
    try:
        android = read_context()
    except RuntimeError:
        adb = [os.environ.get('GB_ADB', 'adb'), '-s', os.environ.get('GB_ANDROID_SERIAL', 'emulator-5554')]
        if restart_android:
            subprocess.run(adb + ['shell', 'am', 'force-stop', 'com.miHoYo.ys.bilibili'], capture_output=True, timeout=10)
            subprocess.run(adb + ['shell', 'am', 'start', '-n', 'com.miHoYo.ys.bilibili/com.miHoYo.GetMobileInfo.MainActivity'], capture_output=True, timeout=15)
        print(json.dumps({'state': 'waiting_for_current_android_context', 'restart_requested': restart_android}), flush=True)
        deadline = time.monotonic() + 180
        while True:
            try:
                android = read_context(); break
            except RuntimeError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(3)
    report = {'private_account_context_persisted': False, 'world_entry_verified': False, 'cases': []}
    output = ROOT / 'private/device-logs/v0.15-publication-profile-controls.json'
    compatible = False
    print(json.dumps({'state': 'contexts_ready_in_memory', 'account_identifier_matches': ios['aid'] == android['aid']}), flush=True)
    for line in sys.stdin:
        command = json.loads(line)
        operation = command.get('op')
        if operation == 'resources':
            left, left_hashes = metadata('ios_official_anonymous', 'cngfdispatch.yuanshen.com',
                dict(ios, channel_id='1', account_type='1', aid='0'))
            time.sleep(3)
            right, right_hashes = metadata('bili_catalog_anonymous_ios_platform', 'cnqddispatch.yuanshen.com',
                dict(android, platform='1', aid='0'))
            compatible = bool(left_hashes) and left_hashes == right_hashes and left.get('retcode') == right.get('retcode') == 0
            report.update(cases=[left, right], resource_fields_identical=compatible,
                checked_resource_fields=sorted(set(left_hashes) | set(right_hashes), key=int),
                different_resource_fields=[k for k in set(left_hashes) | set(right_hashes) if left_hashes.get(k) != right_hashes.get(k)])
            output.write_text(json.dumps(report, indent=2), encoding='utf-8')
            print(json.dumps(report), flush=True)
        elif operation == 'export_catalog':
            if not compatible:
                print(json.dumps({'state': 'export_refused_without_compatible_anonymous_controls'}), flush=True); continue
            # Export only a verified publication selector, never an account/session key.
            text = '/* B publication catalog, anonymously accepted; no account/session fields. */\n'
            text += 'static const char catalog_source_version[] = ' + json.dumps(android['version']) + ';\n'
            text += 'static const char catalog_native_version[] = ' + json.dumps(ios['version']) + ';\n'
            text += 'static const char catalog_selector_uri[] = ' + json.dumps(quote(android['dispatchSeed'], safe='')) + ';\n'
            header = ROOT / 'private/GBReleaseCatalogProfile.h'; header.write_text(text, encoding='utf-8')
            report['catalog_profile_exported'] = True
            report['catalog_selector_fingerprint'] = hashlib.sha256(android['dispatchSeed'].encode()).hexdigest()
            report['catalog_source_version'] = android['version']
            output.write_text(json.dumps(report, indent=2), encoding='utf-8')
            print(json.dumps({'state': 'catalog_profile_exported', 'contains_account_fields': False, 'file': str(header)}), flush=True)
        elif operation == 'finish':
            break
        else:
            print(json.dumps({'state': 'unsupported_operation'}), flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--restart-android', action='store_true')
    args = parser.parse_args()
    try:
        main(args.restart_android)
    except Exception as error:
        print(json.dumps({'state': 'failed', 'error_type': type(error).__name__}), flush=True)
