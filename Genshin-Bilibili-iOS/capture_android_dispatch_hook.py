"""Explicit diagnostic attachment; private context remains in memory, reports are scrubbed."""
import os
import argparse
import asyncio
import hashlib
import importlib.util
import json
from pathlib import Path
import queue
import re
import subprocess
import sys
import threading
import time
from urllib.parse import urlsplit, parse_qsl

ROOT = Path(__file__).resolve().parents[1]
HOSTS = {'dispatchcnglobal.yuanshen.com', 'cngfdispatch.yuanshen.com', 'cnqddispatch.yuanshen.com'}
PARAMS = {'version', 'platform', 'channel_id', 'sub_channel_id', 'account_type', 'key_id',
          'lang', 'binary', 'region', 'region_name', 'server_name'}

def public_context(context):
    url = urlsplit(context.get('url', ''))
    if url.scheme != 'https' or url.hostname not in HOSTS or url.path not in ('/query_region_list', '/query_cur_region'):
        return None
    return {'host': url.hostname, 'path': url.path, 'method': context.get('method', 'unknown'),
            'params': {k: v[:120] for k, v in parse_qsl(url.query) if k in PARAMS},
            'header_names': sorted(context.get('headers', {}))}

def load_inspector():
    path = ROOT / '.work/inspect_dispatch_cache.py'
    spec = importlib.util.spec_from_file_location('dispatch_hook_inspector', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def target_pid(adb, serial, spawn=False):
    base = [str(adb), '-s', serial]
    if spawn:
        package = subprocess.run(base + ['shell', 'dumpsys', 'package', 'com.miHoYo.ys.bilibili'],
            capture_output=True, text=True, timeout=10).stdout
        if not re.search(rf'\buserId={int(os.environ["GB_ANDROID_UID"])}\b', package):
            raise RuntimeError('Target package UID mismatch')
        return None
    result = subprocess.run(base + ['shell', 'pidof', 'com.miHoYo.ys.bilibili'], capture_output=True,
                            text=True, timeout=10)
    pids = result.stdout.split()
    if not pids or not pids[0].isdecimal():
        raise RuntimeError('Target game not running')
    pid = pids[0]
    status = subprocess.run(base + ['shell', "su -c 'cat /proc/" + pid + "/status'"],
                            capture_output=True, text=True, timeout=10).stdout
    if not re.search(rf'^Uid:\s+{int(os.environ["GB_ANDROID_UID"])}\b', status, flags=re.M):
        raise RuntimeError('Target UID mismatch')
    return int(pid)

def main(args):
    # Loaded only for explicit execution, so redaction checks need no Frida service.
    sys.path.insert(0, str(ROOT / '.tools/frida'))
    import frida
    if not frida.__version__.startswith('16.'):
        raise RuntimeError('This prepared agent expects the Frida 16 Java bridge')
    inspector = load_inspector() if (args.validate_original or args.publication_controls) else None
    ios_params = None
    if args.publication_controls:
        ios_rows = asyncio.run(asyncio.wait_for(inspector.read_entries(), timeout=25))
        if not ios_rows:
            raise RuntimeError('iOS cached context required before diagnostic startup')
        ios_params = ios_rows[-1][1]
    pid = target_pid(args.adb, args.serial, args.spawn)
    device = frida.get_device_manager().add_remote_device('127.0.0.1:27042')
    if args.spawn:
        pid = device.spawn(['com.miHoYo.ys.bilibili'])
    report = {'state': 'attaching', 'world_entry_verified': False, 'events': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    save(); pending = queue.Queue(); stopped = threading.Event(); seen = set(); replayed = set()
    session = script = None
    resumed = not args.spawn
    def receive(message, data):
        if message.get('type') == 'send' and isinstance(message.get('payload'), dict):
            pending.put(message['payload'])
        elif message.get('type') == 'error':
            pending.put({'kind': 'diagnostic_state', 'state': 'agent_error'})
    try:
        session = device.attach(pid)
        def detached(reason, crash=None):
            report['detach_reason'] = reason if reason in ('application-requested', 'process-replaced',
                'process-terminated', 'connection-terminated', 'device-lost') else 'other'
            report['crash_report_present'] = crash is not None
            stopped.set()
        session.on('detached', detached)
        script = session.create_script(Path(__file__).with_name('android_dispatch_agent.js').read_text(encoding='utf-8'))
        script.on('message', receive)
        script.load()
        if args.spawn:
            device.resume(pid); resumed = True
        report['state'] = 'observing'; save()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline and not stopped.is_set():
            try:
                payload = pending.get(timeout=1)
            except queue.Empty:
                continue
            if payload.get('kind') == 'diagnostic_state':
                row = {k: payload[k] for k in ('kind', 'state', 'method', 'hook_count') if k in payload}
            elif payload.get('kind') in ('dispatch_constructed', 'dispatch_context'):
                context = payload.get('context') or {'url': payload.get('url', '')}
                row = public_context(context)
                if row is None:
                    continue
                row['kind'] = payload['kind']
                if type(payload.get('http_status')) is int:
                    row['http_status'] = payload['http_status']
                if args.publication_controls and payload['kind'] == 'dispatch_context' and row['method'] == 'GET' and row['path'] == '/query_cur_region':
                    from publication_controls import run_publication_controls, export_publication_profile
                    report['publication_controls'] = run_publication_controls(context, ios_params, inspector)
                    if args.export_catalog_if_compatible and report['publication_controls'].get('state') == 'compatible_metadata_candidate':
                        report['publication_profile_export'] = export_publication_profile(context, ios_params, report['publication_controls'])
                    report['state'] = 'publication_controls_completed'
                    report['events'].append(row); save()
                    print(json.dumps(report['publication_controls']), flush=True)
                    break
                # Only a GET that has already reached the response API can be replayed.
                if inspector and payload['kind'] == 'dispatch_context' and row['method'] == 'GET' and row['path'] == '/query_cur_region':
                    identity = hashlib.sha256(context['url'].encode()).digest()
                    if identity not in replayed and len(replayed) < 3:
                        replayed.add(identity)
                        headers = {k: v for k, v in context.get('headers', {}).items() if k.lower() != 'accept-encoding'}
                        try:
                            response = inspector.requests.get(context['url'], headers=headers, timeout=15, allow_redirects=False)
                            row['metadata_validation'] = {'http': response.status_code}
                            if response.status_code == 200:
                                row['metadata_validation'].update(inspector.summarize(response.content))
                        except Exception as error:
                            row['metadata_validation'] = {'error_type': type(error).__name__}
            else:
                continue
            identity = json.dumps(row, sort_keys=True)
            if identity not in seen:
                seen.add(identity); report['events'].append(row); save()
                print(json.dumps(row), flush=True)
        if report['state'] != 'publication_controls_completed':
            report['state'] = 'detached' if stopped.is_set() else 'capture_window_elapsed'
    except BaseException as error:
        report['state'] = 'diagnostic_failed'
        report['failure_type'] = type(error).__name__
        raise
    finally:
        if not resumed:
            try:
                device.resume(pid)
            except Exception:
                pass
        try:
            if script:
                script.unload()
        except Exception:
            pass
        try:
            if session:
                session.detach()
        except Exception:
            pass
        save()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--serial', default=os.environ.get('GB_ANDROID_SERIAL', 'emulator-5554'))
    parser.add_argument('--seconds', type=int, default=180)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--validate-original', action='store_true')
    parser.add_argument('--publication-controls', action='store_true', help='One in-memory batch: successful baseline, anonymous lookup, full resource-field comparison')
    parser.add_argument('--export-catalog-if-compatible', action='store_true', help='Save only the verified publication version/selector for a local candidate; never account/login fields')
    parser.add_argument('--spawn', action='store_true', help='Restart the game with the agent loaded before startup requests')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 600:
        parser.error('Capture is bounded to 1..600 seconds')
    if args.export_catalog_if_compatible and not args.publication_controls:
        parser.error('Export requires the anonymous publication controls')
    main(args)
