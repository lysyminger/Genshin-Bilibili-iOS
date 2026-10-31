"""Read only Yuanshen dispatch URL metadata; never persist raw syslog lines."""
import asyncio
from datetime import datetime
import json
from pathlib import Path
import re
import sys
from urllib.parse import parse_qsl, urlsplit

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / '.tools/pymobiledevice3'
sys.path[:0] = [str(BASE), str(BASE / 'win32'), str(BASE / 'win32/lib')]
from pymobiledevice3.usbmux import list_devices
from pymobiledevice3.lockdown import create_using_usbmux
from pymobiledevice3.services.os_trace import OsTraceService, OsActivityStreamFlag

SAFE_PARAMS = {'version', 'platform', 'channel_id', 'sub_channel_id', 'lang',
               'binary', 'key_id', 'region', 'account_type'}
HOSTS = {'dispatchcnglobal.yuanshen.com', 'cngfdispatch.yuanshen.com',
         'cnqddispatch.yuanshen.com'}

def extract(message):
    result = []
    for match in re.finditer(r'https?://[^\s<>"\x00]+', message):
        try:
            url = urlsplit(match.group().rstrip("'),;]"))
            if url.hostname not in HOSTS or url.path not in ('/query_region_list', '/query_cur_region'):
                continue
            params = {k: v[:120] for k, v in parse_qsl(url.query) if k in SAFE_PARAMS}
            result.append({'host': url.hostname, 'path': url.path, 'params': params})
        except ValueError:
            continue
    if 'not match region' in message.lower():
        result.append({'error': 'not match region'})
    return result

async def run():
    devices = [d for d in await list_devices() if d.connection_type == 'USB']
    if len(devices) != 1:
        raise RuntimeError('Expected exactly one USB device')
    lockdown = await create_using_usbmux(serial=devices[0].serial, autopair=False, connection_type='USB')
    output = ROOT / 'private/device-logs' / ('dispatch-metadata-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.json')
    events = []
    try:
        print('Waiting for Yuanshen process; raw messages will not be saved.', flush=True)
        deadline = asyncio.get_running_loop().time() + 150
        pid = None
        while asyncio.get_running_loop().time() < deadline:
            async with OsTraceService(lockdown) as trace:
                processes = await trace.get_pid_list()
            for candidate, info in processes.get('Payload', {}).items():
                if isinstance(info, dict) and info.get('ProcessName', '').rsplit('/', 1)[-1] == 'Yuanshen':
                    pid = int(candidate)
                    break
            if pid is not None:
                break
            await asyncio.sleep(2)
        if pid is None:
            print('App was not launched within capture window.', flush=True)
            return
        print('Yuanshen found; capturing whitelisted dispatch metadata for 80 seconds.', flush=True)
        async def capture():
            flags = (OsActivityStreamFlag.PAYLOAD | OsActivityStreamFlag.HISTORICAL |
                     OsActivityStreamFlag.DEBUG | OsActivityStreamFlag.INFO | OsActivityStreamFlag.NO_SENSITIVE)
            async with OsTraceService(lockdown) as trace:
                async for entry in trace.syslog(pid=pid, stream_flags=int(flags)):
                    if entry.pid != pid:
                        continue
                    for item in extract(entry.message):
                        item['timestamp'] = str(entry.timestamp)
                        if item not in events:
                            events.append(item)
                            output.write_text(json.dumps(events, ensure_ascii=False, indent=2), encoding='utf-8')
                            print(json.dumps(item, ensure_ascii=True), flush=True)
        try:
            await asyncio.wait_for(capture(), timeout=80)
        except asyncio.TimeoutError:
            pass
        output.write_text(json.dumps(events, ensure_ascii=False, indent=2), encoding='utf-8')
        print('Capture complete; events=' + str(len(events)) + '; file=' + str(output), flush=True)
    finally:
        await lockdown.close()

if __name__ == '__main__':
    asyncio.run(asyncio.wait_for(run(), timeout=250))
