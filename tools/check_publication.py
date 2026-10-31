"""Inspect actual indexed blobs (or a commit tree), never print secret values."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {'.py', '.c', '.h', '.js', '.S', '.tbd', '.md', '.json', '.yml', '.yaml',
           '.txt', '.cmd', '.ps1', '.toml'}
PATTERNS = {
    'github_token': re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})'),
    'private_key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----'),
    'personal_windows_path': re.compile(rb'(?i)[a-z]:[\\/]+(?:users|development|software)[\\/]'),
    'phone_number': re.compile(rb'(?<![0-9a-fA-F])1[3-9][0-9]{9}(?![0-9a-fA-F])'),
    'long_account_query': re.compile(rb'[?&](?:aid|uid|open_id)=[0-9]{6,}'),
    'email_address': re.compile(rb'[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'),
}


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def private_values(root):
    """Load a local denylist if present, without writing values to a report."""
    directory = root / 'private'
    values = set()
    header = directory / 'GBReleaseCatalogProfile.h'
    if header.is_file():
        text = header.read_text(encoding='utf-8')
        values.update(re.findall(r'catalog_selector_uri\[\]\s*=\s*"([^"\n]+)"', text))
    baseline = directory / 'repro_baseline.json'
    if baseline.is_file():
        config = json.loads(baseline.read_text(encoding='utf-8'))
        values.update(config.get(k, '') for k in ['installed_bundle_id', 'signing_team_id'])
    research = directory / 'seed_research.json'
    if research.is_file():
        data = json.loads(research.read_text(encoding='utf-8'))
        for key, value in data.items():
            if isinstance(value, str):
                values.add(value)
            elif isinstance(value, dict):
                identifiers = {'seedId', 'seedIdHex', 'seedTime', 'fp', 'deviceId', 'hwId',
                               'udid', 'buvid', 'localBdId', 'androidIdB64', 'uid'}
                values.update(v for k, v in value.items() if k in identifiers and isinstance(v, str))
    denylist = directory / 'publication-denylist.json'
    if denylist.is_file():
        values.update(json.loads(denylist.read_text(encoding='utf-8')))
    return {value.encode('utf-8') for value in values if len(value) >= 8}


def review_file(name, data, secrets=()):
    path = PurePosixPath(name)
    findings = []
    forbidden = {'private', 'inputs', 'downloads', '.tools', '.work', 'build', 'output',
                 'device-logs', 'device-crashes', 'android-prefs', '__pycache__'}
    if any(part.lower() in forbidden for part in path.parts):
        findings.append('private_or_generated_path')
    if path.suffix not in ALLOWED and path.name not in {'.gitignore', '.gitattributes'}:
        findings.append('unexpected_file_type')
    if len(data) > 1024 * 1024 or b'\0' in data:
        findings.append('binary_or_oversized_file')
    for label, pattern in PATTERNS.items():
        if pattern.search(data):
            findings.append(label)
    if any(value in data for value in secrets):
        findings.append('local_private_value')
    return findings


def check(root=ROOT, tree=None):
    if tree:
        records = git(root, 'ls-tree', '-rz', tree).split(b'\0')
        entries = [(row.split(b'\t', 1)[1].decode(), row.split(b'\t', 1)[0].split()[2].decode())
                   for row in records if row]
    else:
        records = git(root, 'ls-files', '--stage', '-z').split(b'\0')
        entries = [(row.split(b'\t', 1)[1].decode(), row.split(b'\t', 1)[0].split()[1].decode())
                   for row in records if row]
    if not entries:
        raise ValueError('No indexed files; stage the public files before checking.')
    secrets = private_values(root)
    findings = []
    for name, sha in entries:
        data = git(root, 'cat-file', 'blob', sha)
        for label in review_file(name, data, secrets):
            findings.append({'path': name, 'category': label})
    return {'files_checked': len(entries), 'findings': findings,
            'local_private_reference_loaded': bool(secrets), 'source': tree or 'git_index'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', help='Inspect the blobs of a commit instead of the index')
    args = parser.parse_args()
    try:
        report = check(tree=args.tree)
        print(json.dumps(report, ensure_ascii=False, indent=2))
        raise SystemExit(bool(report['findings']))
    except (ValueError, subprocess.CalledProcessError) as error:
        print(type(error).__name__ + ': publication check could not finish', file=sys.stderr)
        raise SystemExit(1)
