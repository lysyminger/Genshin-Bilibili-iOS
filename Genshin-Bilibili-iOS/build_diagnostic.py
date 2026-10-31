"""Build a passive diagnostic IPA for the supplied Genshin 7.1.0/1241 binary."""
from local_config import local_path
import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import shutil
import struct
import subprocess
import uuid
import zipfile

ROOT = Path(__file__).resolve().parent
APP = 'Payload/Yuanshen.app/'
FRAMEWORK = 'Frameworks/GBDiagnostic.framework/'
LIBRARY = '@executable_path/' + FRAMEWORK + 'GBDiagnostic'
UUID = '19cae6c2-c9e4-3c1f-8b03-1df6671983ea'
DIAGNOSTIC_VERSION = '0.6'
PAGE = 0x4000


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha_file(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def macho(data):
    require(data[:4] == bytes.fromhex('cffaedfe'), 'Expected arm64 Mach-O')
    require(struct.unpack_from('<I', data, 4)[0] == 0x100000c, 'Expected arm64 architecture')
    count, span = struct.unpack_from('<II', data, 16)
    end = 32 + span
    require(end <= len(data), 'Truncated Mach-O commands')
    commands, first_section, identity, dependencies = [], len(data), None, []
    pos = 32
    for _ in range(count):
        cmd, size = struct.unpack_from('<II', data, pos)
        require(size >= 8 and pos + size <= end, 'Malformed load command')
        if cmd in (0x21, 0x2c):
            require(struct.unpack_from('<I', data, pos + 16)[0] == 0, 'Encrypted executable; decryption is not provided')
        if cmd == 0x1b:
            identity = str(uuid.UUID(bytes=bytes(data[pos + 8:pos + 24])))
        if cmd == 0x19:
            for index in range(struct.unpack_from('<I', data, pos + 64)[0]):
                section = pos + 72 + index * 80
                offset = struct.unpack_from('<I', data, section + 48)[0]
                if offset:
                    first_section = min(first_section, offset)
        if cmd in (0xc, 0x80000018, 0x8000001f):
            offset = struct.unpack_from('<I', data, pos + 8)[0]
            dependencies.append(bytes(data[pos + offset:pos + size]).split(b'\0')[0].decode())
        commands.append((cmd, pos, size))
        pos += size
    require(pos == end, 'Load command size mismatch')
    return commands, first_section, identity, dependencies


def inject(data):
    commands, first_section, identity, deps = macho(data)
    require(identity == UUID, 'Unsupported executable UUID')
    require(LIBRARY not in deps, 'Input is already a diagnostic package')
    end = 32 + struct.unpack_from('<I', data, 20)[0]
    path = LIBRARY.encode() + b'\0'
    size = (24 + len(path) + 7) & ~7
    require(end + size <= first_section, 'No room in the Mach-O header')
    require(not any(data[end:end + size]), 'Header padding is not empty')
    edited = bytearray(data)
    command = struct.pack('<IIIIII', 0xc, size, 24, 0, 0x10000, 0x10000) + path
    edited[end:end + size] = command.ljust(size, b'\0')
    struct.pack_into('<II', edited, 16, len(commands) + 1, end + size - 32)
    require(macho(edited)[3] == deps + [LIBRARY], 'Dependency verification failed')
    return bytes(edited)


def require_signing_room(data):
    commands, first_section, _, _ = macho(data)
    end = 32 + struct.unpack_from('<I', data, 20)[0]
    if not any(cmd == 0x1d for cmd, _, _ in commands):
        require(first_section >= end + 16 and not any(data[end:end + 16]),
                'No safe header padding for a new LC_CODE_SIGNATURE')
    return first_section - end


def framework_info():
    return plistlib.dumps({
        'CFBundleExecutable': 'GBDiagnostic', 'CFBundleIdentifier': 'dev.local.GBDiagnostic',
        'CFBundleInfoDictionaryVersion': '6.0', 'CFBundleName': 'GBDiagnostic',
        'CFBundlePackageType': 'FMWK', 'CFBundleShortVersionString': DIAGNOSTIC_VERSION,
        'CFBundleSignature': '????', 'CFBundleSupportedPlatforms': ['iPhoneOS'],
        'CFBundleVersion': DIAGNOSTIC_VERSION, 'MinimumOSVersion': '13.0', 'UIDeviceFamily': [1, 2],
    }, fmt=plistlib.FMT_BINARY)


def normalize_library(data):
    """Match Apple's linker output: page-sized segments and no LC_LOAD_DYLINKER in a dylib."""
    edited = bytearray(data)
    commands = macho(edited)[0]
    segments = [pos for cmd, pos, _ in commands if cmd == 0x19]
    for pos, following in zip(segments, segments[1:]):
        vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', edited, pos + 24)
        next_vmaddr, _, next_fileoff, _ = struct.unpack_from('<QQQQ', edited, following + 24)
        span = next_vmaddr - vmaddr
        require(span == next_fileoff - fileoff and span % PAGE == 0 and vmsize <= span,
                'Segments are not laid out on page boundaries')
        require(not any(edited[fileoff + filesize:next_fileoff]), 'Segment gap is not empty')
        struct.pack_into('<QQQQ', edited, pos + 24, vmaddr, span, fileoff, span)
    for cmd, pos, size in commands:
        if cmd == 0xe:
            count, span = struct.unpack_from('<II', edited, 16)
            end = 32 + span
            edited[pos:end] = edited[pos + size:end] + bytes(size)
            struct.pack_into('<II', edited, 16, count - 1, span - size)
    macho(edited)
    return bytes(edited)


def bind_table_sizes(data):
    """Return the regular and lazy bind opcode sizes from LC_DYLD_INFO_ONLY."""
    pos = next(pos for cmd, pos, _ in macho(data)[0] if cmd == 0x80000022)
    fields = struct.unpack_from('<10I', data, pos + 8)
    return fields[3], fields[7]


def build_library(bridge=False, bili_component=False, combo_login=False, dispatch_trace=False, dispatch_route=False, dispatch_profile=False, region_directory=False, dispatch_stack=False, url_writer=False, catalog_profile=False):
    require(not combo_login or (bridge and bili_component), 'Combo login requires bridge and Bili component')
    require(not dispatch_route or (combo_login and dispatch_trace), 'Dispatch routing requires verified combo state and dispatch tracing')
    require(not dispatch_profile or (dispatch_route and combo_login and dispatch_trace), 'Dispatch profile requires dispatch routing and tracing')
    require(not region_directory or (combo_login and dispatch_trace), 'Directory experiment requires the full login bridge and dispatch tracing')
    require(not dispatch_stack or dispatch_trace, 'Construction stack requires dispatch tracing')
    require(not url_writer or dispatch_stack, 'URL writer observation requires stack diagnostics')
    require(not catalog_profile or dispatch_profile, 'Catalog pairing requires dispatch profile adaptation')
    compiler = local_path('zig_relative', '.tools/zig13/ziglang/zig.exe')
    require(compiler.is_file(), 'Build compiler is missing: .tools/zig13/ziglang/zig.exe (ziglang 0.13.0)')
    output = ROOT / 'build/GBDiagnostic.dylib'
    output.parent.mkdir(exist_ok=True)
    subprocess.run([
        str(compiler), 'cc', '-target', 'aarch64-ios.13.0', '-O2', '-dynamiclib',
        '-nostdlib', '-fno-stack-protector', '-fno-builtin', '-fvisibility=hidden',
        '-DGB_DIAGNOSTIC_VERSION="' + DIAGNOSTIC_VERSION + '"',
        *(['-DGB_ENABLE_BRIDGE=1'] if bridge else []),
        *(['-DGB_ENABLE_BILI_COMPONENT=1'] if bili_component else []),
        *(['-DGB_ENABLE_COMBO_LOGIN=1'] if combo_login else []),
        *(['-DGB_ENABLE_DISPATCH_TRACE=1'] if dispatch_trace else []),
        *(['-DGB_ENABLE_DISPATCH_ROUTE=1'] if dispatch_route else []),
        *(['-DGB_ENABLE_DISPATCH_PROFILE=1'] if dispatch_profile else []),
        *(['-DGB_ENABLE_DISPATCH_STACK=1'] if dispatch_stack else []),
        *(['-DGB_ENABLE_URL_WRITER=1'] if url_writer else []),
        *(['-DGB_ENABLE_CATALOG_PROFILE=1'] if catalog_profile else []),
        *(['-DGB_ENABLE_REGION_DIRECTORY=1', '-DGB_BILI_REGION_ONLY=1'] if region_directory else []),
        '-L' + str(ROOT / 'src/stubs'), '-Wl,-install_name,@rpath/GBDiagnostic.framework/GBDiagnostic',
        '-Wl,-headerpad,0x1000',
        '-o', str(output), str(ROOT / 'src/GBDiagnostic.c'),
    ], check=True)
    output.write_bytes(normalize_library(output.read_bytes()))
    regular, lazy = bind_table_sizes(output.read_bytes())
    require(regular > 0 and lazy == 0, 'Expected only regular binds in the library')
    require(require_signing_room(output.read_bytes()) >= 0x1000,
            'Linker did not reserve the requested signing header padding')
    return output


def package(source, target, library, bridge=False, sdk_candidate=None, combo_login=False, dispatch_route=False, dispatch_profile=False, region_directory=False, dispatch_stack=False, url_writer=False, catalog_profile=False):
    require(source.resolve() != target.resolve(), 'Input and output must differ')
    require(not target.exists(), 'Refusing to overwrite an existing output')
    partial = target.with_suffix('.ipa.partial')
    require(not partial.exists(), 'An unfinished output already exists')
    report = {'purpose': ('Bili login and Genshin combo authentication integration; runtime unverified' if combo_login else
                         'Bili SDK component experiment; game-server authentication not connected' if sdk_candidate is not None else
                         'login channel chooser experiment; Bili provider not connected' if bridge else
                         'passive SDK diagnostics, not Bilibili login'), 'runtime_tested': False,
              'bridge_chooser_enabled': bridge, 'bilibili_provider_included': sdk_candidate is not None,
              'bilibili_provider_runtime_tested': False,
              'bilibili_component_experiment': sdk_candidate is not None,
              'game_server_authentication_implemented': combo_login,
              'dispatch_routing_enabled': dispatch_route, 'dispatch_routing_runtime_tested': False,
              'dispatch_profile_enabled': dispatch_profile, 'dispatch_profile_runtime_tested': False,
              'dispatch_stack_trace_enabled': dispatch_stack, 'dispatch_stack_trace_runtime_tested': False,
              'url_writer_trace_enabled': url_writer, 'url_writer_trace_runtime_tested': False,
              'catalog_pairing_enabled': catalog_profile, 'catalog_pairing_runtime_tested': False,
              'client_directory_experiment': region_directory, 'official_login_available': not region_directory,
              'server_auth_observed': False, 'world_entry_tested': False,
              'diagnostic_version': DIAGNOSTIC_VERSION,
              'library_header_padding_bytes': require_signing_room(library.read_bytes()),
              'bilibili_login_implemented': combo_login, 'signed_with_user_certificate': False,
              'source': str(source.resolve()), 'source_sha256': sha_file(source),
              'modified': [APP + 'Info.plist', APP + 'Yuanshen'], 'added': [], 'removed': []}
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        with zipfile.ZipFile(source) as zin:
            names = zin.namelist()
            require(len(names) == len(set(names)), 'Duplicate ZIP entries')
            for name in names:
                require(not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts and '\\' not in name, 'Unsafe ZIP entry')
            main_apps = [n for n in names if n.startswith('Payload/') and n.endswith('.app/Info.plist') and n.count('/') == 2]
            require(main_apps == [APP + 'Info.plist'], 'Unsupported app layout')
            info = plistlib.loads(zin.read(APP + 'Info.plist'))
            require((info.get('CFBundleIdentifier'), info.get('CFBundleShortVersionString'), str(info.get('CFBundleVersion'))) == ('com.miHoYo.Yuanshen', '7.1.0', '1241'), 'Unsupported app/version/build')
            for entry in zin.infolist():
                if entry.is_dir():
                    continue
                with zin.open(entry) as stream:
                    magic = stream.read(4)
                if magic == bytes.fromhex('cffaedfe'):
                    require_signing_room(zin.read(entry))
                require(magic not in [bytes.fromhex('cafebabe'), bytes.fromhex('cafebabf'), bytes.fromhex('cefaedfe')], 'Additional executable architecture requires inspection')
            info['CFBundleDisplayName'] = '原神桥接实验' if bridge else '原神适配诊断'
            if region_directory:
                info['CFBundleDisplayName'] = '原神世界树实验'
            info['UIFileSharingEnabled'] = True
            info['LSSupportsOpeningDocumentsInPlace'] = True
            info['GBDiagnosticBuild'] = DIAGNOSTIC_VERSION + ('-bridge' if bridge else '-passive')
            additions = ((APP + FRAMEWORK, b'', 0o40755),
                         (APP + FRAMEWORK + 'GBDiagnostic', library.read_bytes(), 0o100755),
                         (APP + FRAMEWORK + 'Info.plist', framework_info(), 0o100644))
            if sdk_candidate is not None:
                extra = []
                for path in sorted(Path(sdk_candidate).rglob('*')):
                    relative = path.relative_to(sdk_candidate).as_posix()
                    if path.name.endswith('.report.json'):
                        continue
                    name = APP + relative + ('/' if path.is_dir() else '')
                    raw = b'' if path.is_dir() else path.read_bytes()
                    if name in names:
                        require(path.is_dir() or zin.read(name) == raw,
                                'SDK resource conflicts with the host: ' + name)
                        continue
                    if raw[:4] == bytes.fromhex('cffaedfe'):
                        require_signing_room(raw)
                    extra.append((name, raw, 0o40755 if path.is_dir() else
                                  (0o100755 if path.name == 'GBBiliSDK' else 0o100644)))
                additions += tuple(extra)
            overlay = {APP + 'Info.plist': plistlib.dumps(info, fmt=plistlib.FMT_BINARY),
                       APP + 'Yuanshen': inject(zin.read(APP + 'Yuanshen'))}
            if url_writer:
                from url_writer_patch import build_stub, patch_writer
                overlay[APP + 'Yuanshen'], report['url_writer_patch'] = patch_writer(overlay[APP + 'Yuanshen'], build_stub())
            overlay.update((name, data) for name, data, _ in additions)
            preserved = {}
            with zipfile.ZipFile(partial, 'w', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as zout:
                for entry in zin.infolist():
                    name = entry.filename
                    parts = PurePosixPath(name).parts
                    if not name.startswith('Payload/') or '_CodeSignature' in parts or 'SC_Info' in parts or name.endswith('/embedded.mobileprovision'):
                        report['removed'].append(name)
                        continue
                    new = copy.copy(entry)
                    new._compresslevel = 1
                    if name in overlay:
                        zout.writestr(new, overlay[name])
                    elif entry.is_dir():
                        zout.writestr(new, b'')
                    else:
                        with zin.open(entry) as src, zout.open(new, 'w', force_zip64=True) as dst:
                            shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
                        preserved[name] = (entry.file_size, entry.CRC)
                for added, data, mode in additions:
                    new = zipfile.ZipInfo(added)
                    new.create_system = 3
                    new.external_attr = mode << 16
                    new.compress_type = zipfile.ZIP_DEFLATED
                    zout.writestr(new, data)
                    report['added'].append(added)
            print('Verifying every output entry and preserved resources...', flush=True)
            with zipfile.ZipFile(partial) as out:
                require(out.testzip() is None, 'Output ZIP CRC failure')
                require(set(out.namelist()) == (set(names) - set(report['removed'])) | set(overlay), 'Output inventory mismatch')
                for name, data in overlay.items():
                    require(out.read(name) == data, 'Modified entry verification failed: ' + name)
                for name, fingerprint in preserved.items():
                    entry = out.getinfo(name)
                    require((entry.file_size, entry.CRC) == fingerprint, 'Preserved entry changed: ' + name)
            report.update(preserved_files=len(preserved), zip_crc_verified=True,
                          output_sha256=sha_file(partial), output_bytes=partial.stat().st_size,
                          library_sha256=sha_file(library))
        require(sha_file(source) == report['source_sha256'], 'Source changed during packaging')
        partial.rename(target)
        target.with_suffix('.report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print('Created diagnostic IPA: ' + str(target), flush=True)
    except BaseException:
        if partial.exists():
            partial.unlink()
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    package(args.source, args.output, build_library())
