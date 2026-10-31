"""Build a dispatch URL construction-stack diagnostic, retaining the v0.16 profile experiment."""
import argparse
from pathlib import Path
import plistlib

import build_diagnostic as diagnostic
from build_bili_component import experiment_config
from prepare_bili_sdk import prepare_sdk

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    candidate = prepare_sdk()
    config = plistlib.loads(experiment_config())
    config.update(gameServerAuthenticationImplemented=True, gameDispatchRoutingImplemented=True,
                  clientDirectoryExperiment=True, directoryRegion='cn_qd01',
                  androidDispatchProfile=True, iosPlatformPreserved=True,
                  dispatchSeedGeneratedByClient=True, dispatchConstructionStackDiagnostic=True,
                  dispatchSeedGenerationLocated=False)
    (candidate / 'GBBiliExperiment.plist').write_bytes(plistlib.dumps(config, fmt=plistlib.FMT_BINARY))
    diagnostic.DIAGNOSTIC_VERSION = '0.17'
    library = diagnostic.build_library(bridge=True, bili_component=True, combo_login=True,
        dispatch_trace=True, dispatch_route=True, dispatch_profile=True, region_directory=True,
        dispatch_stack=True)
    diagnostic.package(args.source, args.output, library, bridge=True, sdk_candidate=candidate,
        combo_login=True, dispatch_route=True, dispatch_profile=True, region_directory=True,
        dispatch_stack=True)
