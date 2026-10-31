"""Build an official-login control with passive SDK tracing and no Bili components."""
import argparse
import json
from pathlib import Path

import build_diagnostic as diagnostic


def build(source, output):
    diagnostic.DIAGNOSTIC_VERSION = '0.20'
    library = diagnostic.build_library()
    diagnostic.package(source, output, library)
    report_path = output.with_suffix('.report.json')
    report = json.loads(report_path.read_text(encoding='utf-8'))
    diagnostic.require(report['official_login_available'] and
        not any(report[key] for key in ['bridge_chooser_enabled', 'bilibili_provider_included',
            'game_server_authentication_implemented', 'dispatch_routing_enabled',
            'dispatch_profile_enabled', 'client_directory_experiment',
            'dispatch_stack_trace_enabled', 'url_writer_trace_enabled', 'catalog_pairing_enabled']),
        'Unexpected login or dispatch modification in passive control')
    report.update(purpose='Official-login control: passive SDK call tracing only; runtime unverified',
                  diagnostic_control='official_login_passive_trace',
                  anti_cheat_or_integrity_check_changes=False,
                  phone_install_or_uninstall_performed=False)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Control ready. Use the same signing identity for an in-place update; do not uninstall.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output)
