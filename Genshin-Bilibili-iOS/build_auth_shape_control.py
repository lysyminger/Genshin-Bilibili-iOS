"""Compare real Bili verification fields with native official callbacks, without B game login."""
import argparse
from pathlib import Path
import plistlib

import build_diagnostic as diagnostic
from build_bili_component import experiment_config
from prepare_bili_sdk import prepare_sdk


def build(source, output):
    candidate = prepare_sdk()
    config = plistlib.loads(experiment_config())
    config.update(gameServerAuthenticationImplemented=True, gameDispatchRoutingImplemented=False,
                  comboVerificationOnly=True, biliGameIdentityDelivered=False,
                  loginShapeLoggingOnly=True)
    (candidate / 'GBBiliExperiment.plist').write_bytes(plistlib.dumps(config, fmt=plistlib.FMT_BINARY))
    diagnostic.DIAGNOSTIC_VERSION = '0.23'
    library = diagnostic.build_library(bridge=True, bili_component=True, combo_login=True,
                                       login_shape=True, verify_only=True)
    diagnostic.package(source, output, library, bridge=True, sdk_candidate=candidate,
                       combo_login=True, login_shape=True, verify_only=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output)
