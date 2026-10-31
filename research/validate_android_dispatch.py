"""Use transient Android dispatch context in memory; persist summaries only."""
import os
import argparse
import asyncio
from datetime import datetime
import json
from pathlib import Path
import subprocess
import sys
import time
from urllib.parse import unquote_plus

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'research'))
from inspect_dispatch_cache import summarize, requests, read_entries

ADB = os.environ.get('GB_ADB', 'adb')
HELPER = '/data/local/tmp/gb-dispatch-scan-v4'
ENDPOINT = 'https://cnqddispatch.yuanshen.com/query_cur_region'

def read_context():
    base = [ADB, '-s', os.environ.get('GB_ANDROID_SERIAL', 'emulator-5554')]
    pids = subprocess.run(base + ['shell', 'pidof', 'com.miHoYo.ys.bilibili'],
                          capture_output=True, text=True, timeout=10).stdout.split()
    if not pids or not pids[0].isdecimal():
        raise RuntimeError('Game process not running')
    result = subprocess.run(base + ['shell', "su -c '" + HELPER + ' ' + pids[0] + " java context-pipe'"],
        capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
    if result.returncode:
        raise RuntimeError('Read-only context extraction failed')
    contexts = []
    for line in result.stdout.splitlines():
        try:
            event = json.loads(line)
        except ValueError:
            continue
        params = event.get('params', {})
        if event.get('path') != '/query_cur_region' or not all(k in params for k in
            ('version', 'platform', 'channel_id', 'account_type', 'key_id', 'dispatchSeed', 'aid')):
            continue
        if params['channel_id'] != '14' or params['account_type'] != '14':
            continue
        contexts.append({k: unquote_plus(v) for k, v in params.items()})
    if not contexts:
        raise RuntimeError('No complete dispatch context currently available')
    return max(contexts, key=lambda item: int(item.get('time', '0')))

def probe(context, name, changes):
    params = dict(context, **changes)
    row = {'name': name, 'public_changes': changes}
    try:
        response = requests.get(ENDPOINT, params=params, timeout=15, allow_redirects=False)
        row['http'] = response.status_code
        if response.status_code == 200:
            row.update(summarize(response.content))
    except Exception as error:
        row['error_type'] = type(error).__name__
    return row

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compare-ios-fields', action='store_true')
    parser.add_argument('--wait-seconds', type=int, default=0)
    args = parser.parse_args()
    if not 0 <= args.wait_seconds <= 600:
        parser.error('Wait window must be bounded to 0..600 seconds')
    report = {'context_persisted': False, 'world_entry_verified': False, 'probes': []}
    try:
        deadline = time.monotonic() + args.wait_seconds
        while True:
            try:
                context = read_context()
                break
            except RuntimeError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(4)
        report['context_field_names'] = sorted(context)
        report['android_public_params'] = {k: v for k, v in context.items()
            if k not in ('aid', 'dispatchSeed', 'time')}
        try:
            entries = asyncio.run(asyncio.wait_for(read_entries(), timeout=25))
            if entries:
                report['account_identifier_matches_ios'] = context['aid'] == entries[-1][1].get('aid')
                report['dispatch_seed_matches_ios'] = context['dispatchSeed'] == entries[-1][1].get('dispatchSeed')
        except Exception as error:
            report['ios_comparison_error_type'] = type(error).__name__
        baseline = probe(context, 'original_android_context', {})
        report['probes'].append(baseline)
        print(json.dumps(baseline), flush=True)
        if args.compare_ios_fields and baseline.get('retcode') == 0 and baseline.get('region_info_present'):
            import time
            cases = [('ios_platform_only', {'platform': '1'}),
                     ('ios_version_only', {'version': 'CNRELiOS7.1.0'}),
                     ('ios_version_and_platform', {'version': 'CNRELiOS7.1.0', 'platform': '1'})]
            for name, changes in cases:
                time.sleep(3)
                result = probe(context, name, changes)
                report['probes'].append(result)
                print(json.dumps(result), flush=True)
                if result.get('http') in (403, 429):
                    break
        del context
    except Exception as error:
        report['error_type'] = type(error).__name__
    output = ROOT / 'private/device-logs' / ('AndroidDispatch-context-validation-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.json')
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Sanitized report: ' + str(output), flush=True)
