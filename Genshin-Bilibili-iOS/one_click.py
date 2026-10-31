"""Prepare the known-good IPA for iloader, or rebuild the supported release locally."""
import argparse
import csv
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import zipfile

PROJECT = Path(__file__).resolve().parent
WORKSPACE = PROJECT.parent
BASELINE = WORKSPACE / 'private/repro_baseline.json'


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def inspect_ipa(path, expected_hash, baseline):
    require(path.is_file(), f'找不到 IPA：{path}')
    print('正在核对 IPA 指纹……', flush=True)
    require(sha256(path) == expected_hash, 'IPA 指纹不符合记录，停止准备；请保留手机上当前可用的应用。')
    with zipfile.ZipFile(path) as archive:
        info = plistlib.loads(archive.read('Payload/Yuanshen.app/Info.plist'))
        config = plistlib.loads(archive.read('Payload/Yuanshen.app/GBBiliExperiment.plist'))
    require(info.get('CFBundleIdentifier') == baseline['source_bundle_id'],
            '输入不是原始 Bundle ID 的待签名包。不要把已追加 Team 后缀的已签名包再次导入。')
    require(info.get('CFBundleShortVersionString') == baseline['game_version'] and
            str(info.get('CFBundleVersion')) == baseline['game_build'] and
            info.get('GBDiagnosticBuild') == baseline['bridge_version'] + '-bridge' and
            config.get('catalogPairingExperiment') is True, '输入不是本次已验证的版本/seed配套构建。')


def preflight(source, baseline):
    donor = WORKSPACE / baseline['sdk_donor_relative']
    compiler = WORKSPACE / baseline.get('zig_relative', '.tools/zig13/ziglang/zig.exe')
    proof = WORKSPACE / baseline['proof_relative']
    status = {'source_ipa_present': source.is_file(), 'sdk_donor_present': donor.is_file(),
              'zig_present': compiler.is_file(), 'proof_present': proof.is_file(),
              'publication_header_present': (WORKSPACE / 'private/GBReleaseCatalogProfile.h').is_file()}
    if all(status.values()):
        from build_catalog_pairing import verified_profile
        verified_profile(proof)
        status['proof_and_profile_valid'] = True
        version = subprocess.run([str(compiler), 'version'], capture_output=True,
                                 text=True, check=True).stdout.strip()
        require(version == baseline['zig_version'], 'Zig版本不匹配，请使用已验证的0.13.0。')
        status['zig_version'] = version
    return status, proof


def rebuild(source, baseline):
    status, proof = preflight(source, baseline)
    require(all(status.values()), '重建材料不齐：' + json.dumps(status, ensure_ascii=False) +
            '\n编译器安装：python -m pip install --target .tools/zig13 ziglang==0.13.0')
    require(sha256(source) == baseline['source_ipa_sha256'],
            '原始IPA不是本次成功的7.1.0/1241输入；新版本需要先适配，不能直接强行构建。')
    require(shutil.disk_usage(PROJECT).free >= 4 * 1024**3, '电脑空间不足，重建至少需要4GiB可用空间。')
    build = PROJECT / 'build'
    build.mkdir(exist_ok=True)
    require(build.resolve().is_relative_to(PROJECT), '构建目录指向工程外，停止。')
    output = PROJECT / 'output' / ('Yuanshen_7.1.0_1241_bili_catalog_v0.19_rebuilt_' +
                                 datetime.now().strftime('%Y%m%d-%H%M%S') + '.ipa')
    # Lock only the shared build cache; the successful original IPA is never overwritten.
    with (build / 'one_click.lock').open('a+b') as lock:
        import msvcrt
        lock.seek(0)
        if not lock.read(1):
            lock.write(b'0'); lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            raise ValueError('另一个一键重建正在运行，请等它完成。') from None
        subprocess.run([sys.executable, str(PROJECT / 'build_catalog_pairing.py'), str(source),
            '--proof', str(proof), '--output', str(output)], cwd=WORKSPACE, check=True)
    report = json.loads(output.with_suffix('.report.json').read_text(encoding='utf-8'))
    require(report.get('zip_crc_verified') and report.get('preserved_files') == 717,
            '重建报告未通过完整检查。')
    inspect_ipa(output, report['output_sha256'], baseline)
    return output


def open_tools(ipa, baseline):
    loader = WORKSPACE / baseline['iloader_relative']
    if loader.is_file():
        require(sha256(loader) == baseline['iloader_sha256'], 'iLoader已发生变化，请先核对版本和安装行为后更新基线。')
    subprocess.Popen(['explorer.exe', '/select,', str(ipa)])
    if not loader.is_file():
        print('未找到本地iLoader，请从 https://iloader.app 获取后手动导入上述IPA。')
        return
    tasks = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq ' + loader.name, '/FO', 'CSV', '/NH'],
                           capture_output=True, text=True, errors='replace')
    running = any(row and row[0].casefold() == loader.name.casefold()
                  for row in csv.reader(tasks.stdout.splitlines()))
    if running:
        print('iLoader已经运行，请切换到原窗口导入IPA。')
    else:
        # This is the interactive signing UI; no install command or credentials are supplied.
        os.startfile(str(loader))


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    require(sys.version_info >= (3, 11), '需要Python3.11或更新版本。')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['prepare', 'rebuild'], default='prepare')
    parser.add_argument('--source', type=Path)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--no-open', action='store_true')
    args = parser.parse_args()
    require(os.name == 'nt', '此入口为Windows准备，请参照README在其他系统调整工具链。')
    require(BASELINE.is_file(), '缺少 private/repro_baseline.json；请先参照 templates 和复现说明配置本机材料。')
    baseline = json.loads(BASELINE.read_text(encoding='utf-8'))
    source = args.source.resolve() if args.source else WORKSPACE / baseline['source_ipa_relative']
    ipa = WORKSPACE / baseline['successful_ipa_relative']
    if args.check_only:
        status, _ = preflight(source, baseline)
        print(json.dumps({'rebuild_prerequisites': status, 'successful_ipa_present': ipa.is_file(),
                         'expected_installed_bundle_id': baseline['installed_bundle_id'],
                         'phone_install_or_uninstall_performed': False}, ensure_ascii=False, indent=2))
        require(all(status.values()), '重建材料不完整；日常续签只要成功IPA仍在，可直接用准备入口。')
        return
    if args.mode == 'rebuild':
        ipa = rebuild(source, baseline)
    else:
        inspect_ipa(ipa, baseline['successful_ipa_sha256'], baseline)
    print('\n已准备好待签名IPA：\n' + str(ipa))
    print('\n保留资源的续签条件：')
    print('1. 保留手机上的原神，不要先删除App。')
    print('2. iLoader沿用这次的同一Apple账号/Team：' + baseline['signing_team_id'])
    print('3. 最终Bundle ID应保持：' + baseline['installed_bundle_id'])
    print('4. 用上述待签名源IPA正常安装；若身份不匹配，先停止，不用卸载来排错。')
    print('本脚本没有执行手机安装、卸载或清理。Apple登录/验证码和安装在iLoader里完成。')
    if not args.no_open:
        open_tools(ipa, baseline)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print('未完成：' + str(error), file=sys.stderr)
        raise SystemExit(1)
