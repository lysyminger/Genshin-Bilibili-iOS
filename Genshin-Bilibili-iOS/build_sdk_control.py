"""Compare SDK presence/loading with official login, without Bili game authentication."""
import argparse
import json
from pathlib import Path
import plistlib

import build_diagnostic as diagnostic
from build_bili_component import experiment_config
from prepare_bili_sdk import prepare_sdk


def build(source, output):
    candidate = prepare_sdk()
    config = plistlib.loads(experiment_config())
    config.update(diagnosticControl='sdk_presence_and_initialization_official_login',
                  gameServerAuthenticationImplemented=False,
                  gameDispatchRoutingImplemented=False,
                  sdkCredentialsNotUsedForGameLogin=True)
    (candidate / 'GBBiliExperiment.plist').write_bytes(plistlib.dumps(config, fmt=plistlib.FMT_BINARY))
    diagnostic.DIAGNOSTIC_VERSION = '0.21'
    library = diagnostic.build_library(bridge=True, bili_component=True)
    diagnostic.package(source, output, library, bridge=True, sdk_candidate=candidate)
    report_path = output.with_suffix('.report.json')
    report = json.loads(report_path.read_text(encoding='utf-8'))
    diagnostic.require(report['official_login_available'] and report['bridge_chooser_enabled'] and
        report['bilibili_provider_included'] and not any(report[key] for key in [
            'game_server_authentication_implemented', 'dispatch_routing_enabled',
            'dispatch_profile_enabled', 'client_directory_experiment',
            'dispatch_stack_trace_enabled', 'url_writer_trace_enabled', 'catalog_pairing_enabled']),
        'Unexpected authentication or dispatch modification in SDK control')
    report.update(purpose='Official-login control with SDK files and opt-in SDK initialization; runtime unverified',
                  diagnostic_control='sdk_presence_and_initialization_official_login',
                  anti_cheat_or_integrity_check_changes=False,
                  phone_install_or_uninstall_performed=False)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('First select official login. SDK test can be cancelled, then return to official login in the same process.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output)
