"""Bounded, read-only watcher; device helper returns allowlisted metadata only."""
import os
import argparse
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import subprocess
import time

ADB = os.environ.get('GB_ADB', 'adb')
SERIAL = os.environ.get('GB_ANDROID_SERIAL', 'emulator-5554')
PACKAGE = 'com.miHoYo.ys.bilibili'
HELPER = '/data/local/tmp/gb-dispatch-scan-v3'
SAFE_FIELDS = {'version', 'lang', 'platform', 'binary', 'channel_id', 'sub_channel_id',
               'account_type', 'key_id', 'region', 'region_name', 'server_name'}

def scrub(event):
    if not isinstance(event, dict) or event.get('path') not in ('/query_region_list', '/query_cur_region', 'not_in_fragment'):
        return None
    if event.get('host') not in ('dispatchcnglobal.yuanshen.com', 'cngfdispatch.yuanshen.com',
                                  'cnqddispatch.yuanshen.com', 'not_in_fragment'):
        return None
    params = event.get('params')
    if not isinstance(params, dict):
        return None
    return {'kind': event.get('kind'), 'host': event['host'], 'path': event['path'],
            'params': {k: v for k, v in params.items() if k in SAFE_FIELDS}}

def complete(event):
    params = event['params']
    return event['path'] == '/query_cur_region' and all(k in params for k in
        ('version', 'platform', 'channel_id', 'account_type'))

def watch(output, seconds):
    report = {'state': 'waiting_for_game', 'attempts': 0, 'events': [],
              'expires_after_seconds': seconds, 'world_entry_verified': False}
    def save():
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    save(); seen = set(); deadline = time.monotonic() + seconds
    base = [ADB, '-s', SERIAL]
    while time.monotonic() < deadline:
        try:
            pids = subprocess.run(base + ['shell', 'pidof', PACKAGE], capture_output=True,
                                  text=True, timeout=10).stdout.split()
            if pids and pids[0].isdecimal():
                result = subprocess.run(base + ['shell', "su -c '" + HELPER + ' ' + pids[0] + " java'"],
                    capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=20)
                report['attempts'] += 1; report['last_pid'] = int(pids[0])
                report['last_helper_exit'] = result.returncode
                if result.returncode == 4:
                    report['state'] = 'stopped_target_identity_changed'; save(); return
                report['state'] = 'watching' if result.returncode == 0 else 'waiting_for_read_permission'
                for line in result.stdout.splitlines():
                    try:
                        event = scrub(json.loads(line))
                    except ValueError:
                        continue
                    if event is None:
                        continue
                    identity = json.dumps(event, sort_keys=True)
                    if identity not in seen:
                        seen.add(identity)
                        event['observed_at'] = datetime.now(timezone(timedelta(hours=8))).isoformat()
                        report['events'].append(event)
                        print(json.dumps(event), flush=True)
                    if complete(event):
                        report['state'] = 'cur_region_parameters_found_requires_validation'; save(); return
            else:
                report['state'] = 'waiting_for_game'
        except (OSError, subprocess.TimeoutExpired) as error:
            report['last_error_type'] = type(error).__name__
        save()
        time.sleep(min(20, max(0, deadline - time.monotonic())))
    report['state'] = 'capture_window_elapsed'; save()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=7200)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 7200:
        parser.error('Capture window must be bounded to 1..7200 seconds')
    watch(args.output, args.seconds)
