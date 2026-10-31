"""Fixed, three-request publication controls. Personal contexts remain in memory."""
import base64
import hashlib
import json
from pathlib import Path
import sys
import time
from urllib.parse import parse_qsl, urlsplit
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[1]
RESOURCE_FIELDS = {8, 9, 12, 13, 14, 18, 19, 20, 22, 26, 27, 34, 35}
REQUIRED_FIELDS = {'8', '9', '14', '18', '22'}

def resource_fingerprints(body, inspector):
    sys.path.insert(0, str(ROOT / 'research'))
    from inspect_v15_profile_controls import fields
    obj = inspector.json.loads(body)
    encrypted = base64.b64decode(obj['content'])
    raw = b''.join(inspector.PRIVATE.decrypt(encrypted[i:i+256], inspector.padding.PKCS1v15())
                   for i in range(0, len(encrypted), 256))
    region = next(value for number, wire, value in fields(raw) if number == 3 and wire == 2)
    return {str(number): {'wire': wire, 'sha256': hashlib.sha256(value if isinstance(value, bytes)
            else str(value).encode()).hexdigest()} for number, wire, value in fields(region)
            if number in RESOURCE_FIELDS}

def compatible(left, right):
    return bool(left) and REQUIRED_FIELDS <= set(left) and left == right

def export_publication_profile(android_context, ios_params, controls):
    if controls.get('state') != 'compatible_metadata_candidate':
        raise ValueError('Profile export requires successful anonymous resource controls')
    params = dict(parse_qsl(urlsplit(android_context['url']).query))
    if params.get('version') != 'CNRELAndroid7.1.0' or ios_params.get('version') != 'CNRELiOS7.1.0' or not params.get('dispatchSeed'):
        raise ValueError('Unsupported publication profile')
    # Only these two publication fields are retained. No account or login fields.
    text = '/* Verified B publication catalog pairing; no account/login fields. */\n'
    text += 'static const char catalog_source_version[] = ' + json.dumps(params['version']) + ';\n'
    text += 'static const char catalog_native_version[] = ' + json.dumps(ios_params['version']) + ';\n'
    text += 'static const char catalog_selector_uri[] = ' + json.dumps(quote(params['dispatchSeed'], safe='')) + ';\n'
    text += 'static const char catalog_query_version[] = ' + json.dumps('version=' + params['version']) + ';\n'
    text += 'static const char catalog_query_selector[] = ' + json.dumps('dispatchSeed=' + quote(params['dispatchSeed'], safe='')) + ';\n'
    output = ROOT / 'private/GBReleaseCatalogProfile.h'
    output.write_text(text, encoding='utf-8')
    return {'publication_pairing_config_saved': True, 'contains_account_or_login_fields': False,
            'source_version': params['version'], 'selector_fingerprint': hashlib.sha256(params['dispatchSeed'].encode()).hexdigest()}

def run_publication_controls(android_context, ios_params, inspector):
    url = urlsplit(android_context.get('url', ''))
    if url.scheme != 'https' or url.netloc != 'cnqddispatch.yuanshen.com' or url.path != '/query_cur_region':
        raise ValueError('Out-of-scope Android metadata context')
    params = dict(parse_qsl(url.query))
    if android_context.get('method') != 'GET' or params.get('channel_id') != '14' or params.get('account_type') != '14':
        raise ValueError('Unexpected metadata context')
    report = {'personal_context_persisted': False, 'world_entry_verified': False, 'cases': []}
    cases = [
        ('original_android_metadata', 'cnqddispatch.yuanshen.com', params,
            {k: v for k, v in android_context.get('headers', {}).items() if k.lower() != 'accept-encoding'}),
        ('bili_anonymous_ios_platform', 'cnqddispatch.yuanshen.com', dict(params, platform='1', aid='0'), {}),
        ('ios_official_anonymous', 'cngfdispatch.yuanshen.com',
            dict(ios_params, channel_id='1', account_type='1', aid='0'), {}),
    ]
    fingerprints = {}
    for index, (name, host, query, headers) in enumerate(cases):
        if index:
            time.sleep(3)
        row = {'name': name}
        try:
            response = inspector.requests.get('https://' + host + '/query_cur_region', params=query,
                headers=headers, timeout=15, allow_redirects=False)
            row['http'] = response.status_code
            if response.status_code == 200:
                row.update(inspector.summarize(response.content))
                if row.get('retcode') == 0 and row.get('region_info_present'):
                    fingerprints[name] = resource_fingerprints(response.content, inspector)
        except Exception as error:
            row['error_type'] = type(error).__name__
        report['cases'].append(row)
        if row.get('http') in (403, 429) or row.get('retcode') != 0 or not row.get('region_info_present'):
            report['state'] = 'no_validated_candidate'; return report
    left = fingerprints.get('bili_anonymous_ios_platform', {})
    right = fingerprints.get('ios_official_anonymous', {})
    report.update(resource_fields_identical=compatible(left, right),
        checked_resource_fields=sorted(set(left) | set(right), key=int),
        differing_resource_fields=sorted(k for k in set(left) | set(right) if left.get(k) != right.get(k)),
        state='compatible_metadata_candidate' if compatible(left, right) else 'resource_compatibility_not_established')
    return report
