"""Run a finite, sequential comparison of official dispatch metadata requests."""
import argparse
import asyncio
from datetime import datetime
import importlib.util
import json
from pathlib import Path
import re
import time

ROOT = Path(__file__).resolve().parents[1]
ANDROID_VERSION = 'CNRELAndroid7.1.0_R48145775_S48131320_D48145775'
ENDPOINT = '/query_cur_region'
HOSTS = {'cnqddispatch.yuanshen.com', 'cngfdispatch.yuanshen.com'}
FIELDS = {'version', 'platform', 'region', 'region_name'}

def build_cases(android_version=ANDROID_VERSION):
    if not re.fullmatch(r'CNRELAndroid\d+\.\d+\.\d+(?:_R\d+_S\d+_D\d+)?', android_version):
        raise ValueError('Invalid observed Android version')
    return [
        ('ios-current', 'cnqddispatch.yuanshen.com', {}),
        ('gf-negative-control', 'cngfdispatch.yuanshen.com', {}),
        ('ios-region-name', 'cnqddispatch.yuanshen.com', {'region_name': 'cn_qd01'}),
        ('ios-region', 'cnqddispatch.yuanshen.com', {'region': 'cn_qd01'}),
        ('ios-derived-full-version', 'cnqddispatch.yuanshen.com', {'version': android_version.replace('Android', 'iOS')}),
        ('android-observed-version-ios-platform', 'cnqddispatch.yuanshen.com', {'version': android_version}),
        ('android-observed-version', 'cnqddispatch.yuanshen.com', {'version': android_version, 'platform': '2'}),
        ('android-base-version-ios-platform', 'cnqddispatch.yuanshen.com', {'version': 'CNRELAndroid7.1.0'}),
        ('android-base-version', 'cnqddispatch.yuanshen.com', {'version': 'CNRELAndroid7.1.0', 'platform': '2'}),
        ('pc-base-version-ios-platform', 'cnqddispatch.yuanshen.com', {'version': 'CNRELWin7.1.0'}),
        ('pc-base-version', 'cnqddispatch.yuanshen.com', {'version': 'CNRELWin7.1.0', 'platform': '3'}),
    ]

def metadata_candidate(result):
    return result.get('http') == 200 and result.get('retcode') == 0 and result.get('region_info_present') is True

def load_inspector():
    path = ROOT / '.work/inspect_dispatch_cache.py'
    spec = importlib.util.spec_from_file_location('dispatch_cache_inspector', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def run(inspector, cases, parameters, output, delay):
    report = {'purpose': 'Metadata comparison only; no login attempts or IPA changes',
              'world_entry_verified': False, 'state': 'running', 'cases': []}
    output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    save()
    with inspector.requests.Session() as session:
        for index, (name, host, changes) in enumerate(cases):
            if host not in HOSTS or not set(changes) <= FIELDS:
                raise ValueError('Probe scope exceeded')
            if index:
                time.sleep(delay)
            params = dict(parameters, **changes)
            # No parameters containing account identifiers, nonces or tokens are reported.
            row = {'name': name, 'host': host, 'changes': changes}
            try:
                response = session.get('https://' + host + ENDPOINT, params=params,
                                       timeout=10, allow_redirects=False)
                row['http'] = response.status_code
                if response.status_code == 200:
                    row.update(inspector.summarize(response.content))
            except Exception as error:
                row['error_type'] = type(error).__name__
            report['cases'].append(row)
            print(json.dumps(row), flush=True)
            if row.get('http') in (403, 429):
                report['state'] = 'stopped_by_http_status'; save(); break
            if metadata_candidate(row):
                report['state'] = 'metadata_candidate_requires_client_validation'; save(); break
            save()
        else:
            report['state'] = 'complete_without_metadata_candidate'; save()
    return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--android-version', default=ANDROID_VERSION)
    parser.add_argument('--only', action='append', help='Run only named cases; may be repeated')
    parser.add_argument('--delay', type=float, default=3)
    parser.add_argument('--plan-only', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 2 <= args.delay <= 30:
        parser.error('Delay must be between 2 and 30 seconds')
    cases = build_cases(args.android_version)
    if args.only:
        if set(args.only) - {c[0] for c in cases}:
            parser.error('Unknown case name')
        cases = [c for c in cases if c[0] in args.only]
    if args.plan_only:
        print(json.dumps(cases, indent=2))
    else:
        inspector = load_inspector()
        entries = asyncio.run(asyncio.wait_for(inspector.read_entries(), timeout=25))
        if not entries:
            parser.error('No matching iOS dispatch cache entry; keep the test device connected')
        output = args.output or ROOT / 'private/device-logs' / (
            'DispatchProbes-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.json')
        report = run(inspector, cases, entries[-1][1], output, args.delay)
        print('Finished: ' + report['state'] + '; file=' + str(output), flush=True)
