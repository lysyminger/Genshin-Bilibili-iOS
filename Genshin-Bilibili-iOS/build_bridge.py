"""Build the first Genshin login bridge experiment; Bili provider is not connected."""
import argparse
from pathlib import Path
import build_diagnostic as diagnostic

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    diagnostic.DIAGNOSTIC_VERSION = '0.7'
    diagnostic.package(args.source, args.output, diagnostic.build_library(bridge=True), bridge=True)
