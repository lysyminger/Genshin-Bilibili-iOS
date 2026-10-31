"""Local paths are read from the ignored private directory."""
import json
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parents[1]
PRIVATE = WORKSPACE / 'private'
BASELINE = PRIVATE / 'repro_baseline.json'


def local_path(key, default):
    config = json.loads(BASELINE.read_text(encoding='utf-8')) if BASELINE.is_file() else {}
    return WORKSPACE / config.get(key, default)
