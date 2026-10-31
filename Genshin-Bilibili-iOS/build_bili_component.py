"""Build an opt-in Bili SDK component test; no Genshin B-server login yet."""
from local_config import local_path
import argparse
import json
from pathlib import Path
import plistlib
import zipfile

import build_diagnostic as diagnostic
from prepare_bili_sdk import prepare_sdk


def experiment_config():
    apk = local_path('apk_relative', 'inputs/bilibili.apk')
    with zipfile.ZipFile(apk) as z:
        ext = json.loads(z.read('assets/runtime_config.json'))['ext']
    diagnostic.require(ext['channel_id'] == 14 and ext['jar_name'] == 'com.combosdk.forBzhan.PluginImpl',
                       'Expected the supplied Bili channel APK')
    return plistlib.dumps({'gameId': str(ext['cp_app_id']), 'merchantId': str(ext['cp_id']),
        'serverId': str(ext['server_id']), 'appKey': ext['cp_app_key'], 'sandboxKey': '',
        'parameterSource': 'Android APK; iOS acceptance unverified',
        'gameServerAuthenticationImplemented': False}, fmt=plistlib.FMT_BINARY)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    candidate = prepare_sdk()
    (candidate / 'GBBiliExperiment.plist').write_bytes(experiment_config())
    diagnostic.DIAGNOSTIC_VERSION = '0.8'
    library = diagnostic.build_library(bridge=True, bili_component=True)
    diagnostic.package(args.source, args.output, library, bridge=True, sdk_candidate=candidate)
