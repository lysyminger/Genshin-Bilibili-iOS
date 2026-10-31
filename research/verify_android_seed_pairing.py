"""Two normal Android starts, then fixed metadata controls; private contexts stay in RAM."""
import os
import asyncio
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from urllib.parse import urlencode

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'research'), str(ROOT / 'Genshin-Bilibili-iOS')]
import inspect_dispatch_cache as inspector
from validate_android_dispatch import ADB, read_context
from publication_controls import resource_fingerprints, compatible, export_publication_profile

PACKAGE = 'com.miHoYo.ys.bilibili'
ENDPOINT = 'https://cnqddispatch.yuanshen.com/query_cur_region'
SAFE = {'version', 'platform', 'channel_id', 'sub_channel_id', 'account_type', 'key_id', 'lang', 'binary'}
BASE = [ADB, '-s', os.environ.get('GB_ANDROID_SERIAL', 'emulator-5554')]


def adb(*args):
    return subprocess.run(BASE + list(args), capture_output=True, text=True,
                          encoding='utf-8', errors='replace', timeout=15)


def public(params):
    return {k: v for k, v in params.items() if k in SAFE}


def capture_run(number, previous_pid, report, save):
    stopped = adb('shell', 'am', 'force-stop', PACKAGE)
    if stopped.returncode:
        raise RuntimeError('Normal stop failed')
    launched = adb('shell', 'monkey', '-p', PACKAGE, '-c', 'android.intent.category.LAUNCHER', '1')
    if launched.returncode:
        raise RuntimeError('Normal start failed')
    report['state'] = f'run_{number}_waiting_for_normal_login'
    save()
    print(json.dumps({'state': report['state']}), flush=True)
    deadline = time.monotonic() + 300
    last_status = 0
    while time.monotonic() < deadline:
        try:
            params = read_context()
            pid_text = adb('shell', 'pidof', PACKAGE).stdout.split()
            if not pid_text or not pid_text[0].isdigit():
                continue
            pid = int(pid_text[0])
            if pid == previous_pid:
                raise ValueError('Process did not change')
            if params.get('version') != 'CNRELAndroid7.1.0' or params.get('platform') != '2':
                raise ValueError('Unexpected Android client profile')
            if not re.fullmatch(r'[0-9a-fA-F]{16}', params.get('dispatchSeed', '')):
                raise ValueError('Unexpected seed format')
            row = {'run': number, 'pid': pid, 'params': public(params),
                   'seed_sha256': hashlib.sha256(params['dispatchSeed'].encode()).hexdigest(),
                   'source': 'read_only_target_memory_not_wire_capture'}
            report['runs'].append(row)
            report['state'] = f'run_{number}_context_captured'
            save()
            print(json.dumps({'state': report['state'], 'run': row}), flush=True)
            return pid, params
        except RuntimeError:
            pass
        if time.monotonic() - last_status >= 30:
            print(json.dumps({'state': f'run_{number}_still_waiting_for_dispatch_context'}), flush=True)
            last_status = time.monotonic()
        time.sleep(1)
    raise TimeoutError(f'Run {number} capture window elapsed')


def load_ios():
    rows = asyncio.run(asyncio.wait_for(inspector.read_entries(), timeout=20))
    if not rows:
        raise ValueError('No iOS cache reference')
    params = dict(rows[-1][1])
    if params.get('platform') != '1' or params.get('key_id') != '4':
        raise ValueError('Unexpected iOS reference')
    # v0.16-v0.18 rewrote only the version label; the original iOS seed remained unchanged.
    if hashlib.sha256(params.get('dispatchSeed', '').encode()).hexdigest() != os.environ['GB_NATIVE_IOS_SEED_SHA256']:
        raise ValueError('Unknown native iOS seed reference')
    return dict(params, version='CNRELiOS7.1.0')


def probe(name, host, params):
    row = {'name': name, 'host': host, 'params': public(params), 'cookies_or_authorization_sent': False}
    body = None
    try:
        # A fresh client per case also prevents cookies or .netrc credentials carrying over.
        with inspector.requests.Session() as session:
            session.trust_env = False
            session.headers.clear()
            response = session.get('https://' + host + '/query_cur_region', params=params,
                                   timeout=15, allow_redirects=False)
            row['http'] = response.status_code
            if response.status_code == 200:
                body = response.content
                row.update(inspector.summarize(body))
                if row.get('retcode') == 0 and row.get('region_info_present'):
                    row['resource_fingerprints'] = resource_fingerprints(body, inspector)
    except Exception as error:
        row['error_type'] = type(error).__name__
    return row


def successful(row):
    return row.get('http') == 200 and row.get('retcode') == 0 and bool(row.get('region_info_present'))


def main():
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
    path = ROOT / f'private/device-logs/AndroidSeed-pairing-{stamp}.json'
    report = {'state': 'checking_references', 'private_contexts_persisted': False,
              'world_entry_verified': False, 'runs': [], 'cases': []}
    def save():
        path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    try:
        package = adb('shell', 'dumpsys', 'package', PACKAGE).stdout
        if not re.search(rf'\buserId={int(os.environ["GB_ANDROID_UID"])}\b', package) or not re.search(r'\bversionCode=1241\b', package):
            raise ValueError('Installed Android package identity/version mismatch')
        ios = None
        try:
            ios = load_ios()
            report['ios_reference_loaded'] = True
        except Exception as error:
            report['ios_reference_loaded'] = False
            report['ios_reference_error_type'] = type(error).__name__
        save()
        print(json.dumps({'state': 'references_checked', 'ios_reference_loaded': ios is not None}), flush=True)
        pid, first = capture_run(1, None, report, save)
        print(json.dumps({'state': 'second_normal_start_in_8_seconds'}), flush=True)
        time.sleep(8)
        _, second = capture_run(2, pid, report, save)
        stable = first['dispatchSeed'] == second['dispatchSeed'] and public(first) == public(second)
        report['same_device_same_account'] = first.get('aid') == second.get('aid')
        report['seed_equal_between_independent_processes'] = stable
        if not stable:
            report['state'] = 'seed_or_profile_changed'; save(); return
        cases = [
            ('A_original_android_query', 'cnqddispatch.yuanshen.com', second),
            ('B_no_explicit_account_android', 'cnqddispatch.yuanshen.com', dict(second, aid='0')),
            ('C_no_explicit_account_ios_platform', 'cnqddispatch.yuanshen.com', dict(second, aid='0', platform='1')),
        ]
        for index, (name, host, params) in enumerate(cases):
            if index:
                time.sleep(3)
            row = probe(name, host, params)
            report['cases'].append(row); save()
            print(json.dumps({k: v for k, v in row.items() if k != 'resource_fingerprints'}), flush=True)
            if not successful(row):
                report['state'] = 'metadata_control_failed'; save(); return
        if ios is None:
            report['state'] = 'waiting_for_ios_reference'; save()
            print(json.dumps({'state': report['state']}), flush=True)
            deadline = time.monotonic() + 120
            while ios is None and time.monotonic() < deadline:
                try:
                    ios = load_ios()
                except Exception:
                    time.sleep(3)
        if ios is None:
            report['state'] = 'metadata_passed_resource_reference_missing'; save(); return
        time.sleep(3)
        native = dict(ios, channel_id='1', account_type='1', aid='0')
        row = probe('D_native_ios_official_reference', 'cngfdispatch.yuanshen.com', native)
        report['cases'].append(row)
        if not successful(row):
            report['state'] = 'ios_resource_reference_failed'; save(); return
        left = report['cases'][2]['resource_fingerprints']; right = row['resource_fingerprints']
        report['resource_fields_identical'] = compatible(left, right)
        report['differing_resource_fields'] = sorted(k for k in set(left) | set(right) if left.get(k) != right.get(k))
        report['state'] = 'compatible_metadata_candidate' if compatible(left, right) else 'resource_compatibility_not_established'
        if compatible(left, right):
            context = {'method': 'GET', 'url': ENDPOINT + '?' + urlencode(second)}
            report['publication_profile_export'] = export_publication_profile(context, ios, report)
        save()
    except Exception as error:
        report['state'] = 'verification_failed'; report['error_type'] = type(error).__name__; save()
    finally:
        print(json.dumps({'state': report['state'], 'report_path': str(path),
                          'seed_stable': report.get('seed_equal_between_independent_processes'),
                          'resource_fields_identical': report.get('resource_fields_identical')}), flush=True)


if __name__ == '__main__':
    main()
