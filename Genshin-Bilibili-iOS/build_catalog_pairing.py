"""Build only after two-run stability and fixed anonymous/resource controls succeeded."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re

import build_diagnostic as diagnostic
from build_bili_component import experiment_config
from prepare_bili_sdk import prepare_sdk
from publication_controls import compatible


def verified_profile(proof, profile_header=None):
    report = json.loads(proof.read_text(encoding='utf-8'))
    diagnostic.require(report.get('state') == 'compatible_metadata_candidate' and
        report.get('seed_equal_between_independent_processes') and report.get('resource_fields_identical'),
        'Missing stability or resource control evidence')
    runs = report.get('runs', [])
    diagnostic.require(len(runs) == 2 and runs[0]['pid'] != runs[1]['pid'] and
        runs[0]['seed_sha256'] == runs[1]['seed_sha256'], 'Independent-run evidence is missing')
    cases = {row['name']: row for row in report.get('cases', [])}
    required = ['A_original_android_query', 'B_no_explicit_account_android',
                'C_no_explicit_account_ios_platform', 'D_native_ios_official_reference']
    diagnostic.require(all(name in cases and cases[name].get('http') == 200 and
        cases[name].get('retcode') == 0 and cases[name].get('region_info_present') and
        cases[name].get('cookies_or_authorization_sent') is False for name in required),
        'Missing successful anonymous metadata controls')
    expected = [
        ('A_original_android_query', 'CNRELAndroid7.1.0', '2', '14', '14'),
        ('B_no_explicit_account_android', 'CNRELAndroid7.1.0', '2', '14', '14'),
        ('C_no_explicit_account_ios_platform', 'CNRELAndroid7.1.0', '1', '14', '14'),
        ('D_native_ios_official_reference', 'CNRELiOS7.1.0', '1', '1', '1')]
    for name, version, platform, channel, account in expected:
        params = cases[name].get('params', {})
        diagnostic.require(all(params.get(key) == value for key, value in
            [('version', version), ('platform', platform), ('channel_id', channel), ('account_type', account)]),
            'Unexpected control client profile')
    diagnostic.require(compatible(cases['C_no_explicit_account_ios_platform'].get('resource_fingerprints', {}),
        cases['D_native_ios_official_reference'].get('resource_fingerprints', {})),
        'Resource fingerprints do not match')
    header_path = profile_header or diagnostic.ROOT.parent / 'private/GBReleaseCatalogProfile.h'
    header = header_path.read_text(encoding='utf-8')
    values = {name: json.loads(value) for name, value in re.findall(
        r'static const char (\w+)\[\] = ("[^"\n]*");', header)}
    seed = values.get('catalog_selector_uri', '')
    diagnostic.require(values.get('catalog_native_version') == 'CNRELiOS7.1.0' and
        values.get('catalog_source_version') == 'CNRELAndroid7.1.0' and
        re.fullmatch(r'[0-9a-fA-F]{16}', seed) and
        hashlib.sha256(seed.encode()).hexdigest() == runs[0]['seed_sha256'] and
        values.get('catalog_query_selector') == 'dispatchSeed=' + seed and
        values.get('catalog_query_version') == 'version=CNRELAndroid7.1.0', 'Publication header/proof mismatch')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    verified_profile(args.proof)
    candidate = prepare_sdk()
    config = plistlib.loads(experiment_config())
    config.update(gameServerAuthenticationImplemented=True, gameDispatchRoutingImplemented=True,
                  clientDirectoryExperiment=True, directoryRegion='cn_qd01',
                  androidDispatchProfile=True, iosPlatformPreserved=True, catalogPairingExperiment=True,
                  anonymousMetadataObserved=True, clientSeedGenerationAlgorithmLocated=False,
                  pairingProofSHA256=diagnostic.sha_file(args.proof))
    (candidate / 'GBBiliExperiment.plist').write_bytes(plistlib.dumps(config, fmt=plistlib.FMT_BINARY))
    diagnostic.DIAGNOSTIC_VERSION = '0.19'
    library = diagnostic.build_library(bridge=True, bili_component=True, combo_login=True,
        dispatch_trace=True, dispatch_route=True, dispatch_profile=True, region_directory=True, catalog_profile=True)
    diagnostic.package(args.source, args.output, library, bridge=True, sdk_candidate=candidate,
        combo_login=True, dispatch_route=True, dispatch_profile=True, region_directory=True, catalog_profile=True)
