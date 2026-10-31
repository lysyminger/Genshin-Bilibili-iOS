"""Collect only public dispatch metadata from the Genshin B Android process."""
import argparse
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
from urllib.parse import parse_qsl, urlsplit

PACKAGE = 'com.miHoYo.ys.bilibili'
HOSTS = {'dispatchcnglobal.yuanshen.com', 'cngfdispatch.yuanshen.com', 'cnqddispatch.yuanshen.com'}
PATHS = {'/query_region_list', '/query_cur_region'}
PARAMETERS = {'version', 'platform', 'channel_id', 'sub_channel_id', 'account_type',
              'lang', 'binary', 'key_id', 'region', 'region_name', 'server_name'}

def login_metadata(line):
    if 'combo_token' not in line or len(line) > 65536:
        return []
    events = []
    for match in list(re.finditer(r'\{', line))[:12]:
        try:
            value = json.JSONDecoder().raw_decode(line[match.start():])[0]
        except ValueError:
            continue
        pending = [(value, 0, None)]
        visited = 0
        while pending and visited < 128:
            value, depth, status = pending.pop(); visited += 1
            if depth > 5:
                continue
            if isinstance(value, dict):
                for key in ('ret', 'retcode'):
                    if type(value.get(key)) is int:
                        status = value[key]
                if 'combo_token' in value and 'open_id' in value:
                    account = value.get('account_type')
                    account = int(account) if isinstance(account, str) and account.isdecimal() else account
                    event = {'stage': 'combo_result_fields_logged',
                        'open_id_present': bool(value['open_id']),
                        'combo_token_present': bool(value['combo_token']),
                        'account_type': account if type(account) is int and 0 <= account <= 64 else 'other'}
                    if status is not None:
                        event['reported_status'] = status
                    if event not in events:
                        events.append(event)
                pending.extend((item, depth + 1, status) for item in list(value.values())[:32])
            elif isinstance(value, list):
                pending.extend((item, depth + 1, status) for item in value[:32])
            elif isinstance(value, str) and value.startswith(('{', '[')):
                try:
                    pending.append((json.loads(value), depth + 1, status))
                except ValueError:
                    pass
        if events:
            break
    return events

def extract_metadata(line):
    events = []
    for match in re.finditer(r'https?://[^\s<>"\x00]+', line):
        try:
            url = urlsplit(match.group().rstrip("'),;]"))
            if url.hostname not in HOSTS or url.path not in PATHS:
                continue
            pairs = parse_qsl(url.query)
            events.append({'host': url.hostname, 'path': url.path,
                'params': {key: value[:120] for key, value in pairs if key in PARAMETERS}})
        except ValueError:
            continue
    if 'not match region' in line.lower():
        events.append({'error': 'not match region'})
    events.extend(login_metadata(line))
    return events

def app_line(line, uid):
    match = re.match(r'^\d\d-\d\d\s+\d\d:\d\d:\d\d\.\d+\s+(\S+)\s+(\d+)\s+\d+\s+[VDIWEFAS]\s+[^:]+:\s?(.*)$', line)
    if not match:
        return None
    source = match[1]
    if source == str(uid) or source == 'u0_a' + str(uid - 10000):
        return int(match[2]), match[3]
    return None

def capture(adb, serial, output, wait_seconds=180, capture_seconds=120, uid=None):
    command = [str(adb), '-s', serial]
    report = {'package': PACKAGE, 'events': [], 'state': 'waiting_for_app'}
    output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    save()
    print('Waiting for Genshin B Android process. Raw logcat will not be saved.', flush=True)
    deadline = time.monotonic() + wait_seconds
    pid = None
    while uid is None and time.monotonic() < deadline:
        result = subprocess.run(command + ['shell', 'pidof', PACKAGE], capture_output=True,
                                text=True, timeout=10)
        candidates = result.stdout.split()
        if candidates and candidates[0].isdecimal():
            pid = candidates[0]
            break
        time.sleep(2)
    if not pid and uid is None:
        report['state'] = 'app_not_started'; save()
        print('App was not started within capture window.', flush=True)
        return report
    report['state'] = 'capturing'
    report['app_uid'] = uid
    report['initial_pid'] = int(pid) if pid else None
    save()
    print('Game found; collecting allowlisted dispatch fields.', flush=True)
    pending = queue.Queue()
    options = ['--pid=' + pid, '-v', 'raw'] if uid is None else ['-v', 'threadtime', '-v', 'uid']
    process = subprocess.Popen(command + ['logcat', *options],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding='utf-8', errors='replace')
    def read():
        for line in process.stdout:
            # Discard each raw line here, before passing anything to the consumer.
            source_pid = int(pid) if pid else None
            if uid is not None:
                filtered = app_line(line, uid)
                if filtered is None:
                    continue
                source_pid, line = filtered
            for event in extract_metadata(line):
                event['process_id'] = source_pid
                pending.put(event)
    worker = threading.Thread(target=read, daemon=True)
    worker.start()
    seen = set()
    deadline = time.monotonic() + capture_seconds
    try:
        while time.monotonic() < deadline:
            try:
                event = pending.get(timeout=1)
            except queue.Empty:
                if process.poll() is not None:
                    report['logcat_exit_code'] = process.returncode
                    break
                continue
            identity = json.dumps(event, sort_keys=True)
            if identity in seen:
                continue
            seen.add(identity)
            event['observed_at'] = datetime.now(timezone(timedelta(hours=8))).isoformat()
            report['events'].append(event); save()
            print(json.dumps(event, ensure_ascii=True), flush=True)
    finally:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=10)
        worker.join(timeout=2)
        report['state'] = 'complete'; save()
    print('Capture complete; events=' + str(len(report['events'])) + '; file=' + str(output), flush=True)
    return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--uid', type=int, help='Filter logcat UID in memory, following app process restarts')
    parser.add_argument('--wait-seconds', type=int, default=180)
    parser.add_argument('--capture-seconds', type=int, default=120)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not args.adb.is_file() or not 1 <= args.wait_seconds <= 600 or not 1 <= args.capture_seconds <= 3600:
        parser.error('ADB must exist; wait is bounded to 600 seconds and capture to 3600 seconds')
    output = args.output or Path(__file__).resolve().parents[1] / 'private/device-logs' / (
        'AndroidDispatch-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.json')
    capture(args.adb, args.serial, output, args.wait_seconds, args.capture_seconds, args.uid)
