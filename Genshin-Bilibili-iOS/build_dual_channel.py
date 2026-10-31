"""Keep native official login and an independent Bili experiment in one IPA."""
import argparse
from pathlib import Path
import plistlib

import build_diagnostic as diagnostic
from build_bili_component import experiment_config
from build_catalog_pairing import verified_profile
from prepare_bili_sdk import prepare_sdk


def build(source, proof, output):
    verified_profile(proof)
    candidate = prepare_sdk()
    config = plistlib.loads(experiment_config())
    config.update(gameServerAuthenticationImplemented=True, gameDispatchRoutingImplemented=True,
                  clientDirectoryExperiment=True, directoryRegions=['cn_gf01', 'cn_qd01'],
                  dualLoginExperiment=True, officialLoginAvailable=True,
                  nativeDirectoryRequestPreserved=True, nativeDirectoryConfigPreserved=True,
                  iosPlatformPreserved=True, catalogPairingExperiment=True,
                  pairingAppliedOnlyToCurrentRegion=True, loginShapeLoggingOnly=True,
                  comboVerificationOnly=False, biliWorldEntryVerified=False,
                  pairingProofSHA256=diagnostic.sha_file(proof))
    (candidate / 'GBBiliExperiment.plist').write_bytes(plistlib.dumps(config, fmt=plistlib.FMT_BINARY))
    diagnostic.DIAGNOSTIC_VERSION = '0.24'
    library = diagnostic.build_library(bridge=True, bili_component=True, combo_login=True,
        dispatch_trace=True, dispatch_route=True, dispatch_profile=True, region_directory=True,
        catalog_profile=True, native_directory=True, login_shape=True, dual_login=True)
    diagnostic.package(source, output, library, bridge=True, sdk_candidate=candidate, combo_login=True,
        dispatch_route=True, dispatch_profile=True, region_directory=True, catalog_profile=True,
        native_directory=True, login_shape=True, dual_login=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.proof, args.output)
