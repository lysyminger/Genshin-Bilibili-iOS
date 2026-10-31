"""Prepare a locally supplied SDK candidate without the Arknights bootstrap.

This is an experimental derivative image, not a recovered standalone SDK build.
Game-specific machine code remains dormant in the file; it is not executed by us.
"""
from local_config import local_path
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import uuid
import zipfile

from build_diagnostic import ROOT, macho, require, require_signing_room

SOURCE_HASH = '3deaf5d8291e38d62fe87668c781059c40d61b7a80028761237fe9f7f8b1b16e'
INSTALL_NAME = '@rpath/GBBiliSDK.framework/GBBiliSDK'
SDK_ROOT = 'Payload/Arknights.app/'


def sections(data):
    result = {}
    for cmd, pos, _ in macho(data)[0]:
        if cmd != 0x19:
            continue
        for index in range(struct.unpack_from('<I', data, pos + 64)[0]):
            p = pos + 72 + 80 * index
            name = data[p:p + 16].rstrip(b'\0').decode()
            addr, size, offset = struct.unpack_from('<QQI', data, p + 32)
            result[name] = (addr, size, offset, p)
    return result


def offset(data, address):
    for cmd, pos, _ in macho(data)[0]:
        if cmd == 0x19:
            va, _, start, size = struct.unpack_from('<QQQQ', data, pos + 24)
            if va <= address < va + size:
                return start + address - va
    raise ValueError('Pointer outside file-backed segments')


def class_info(data, address):
    ro = struct.unpack_from('<Q', data, offset(data, address) + 32)[0] & ~7
    name_address = struct.unpack_from('<Q', data, offset(data, ro) + 24)[0]
    p = offset(data, name_address)
    return data[p:data.index(0, p)].decode(), p


def rebase_slots(data):
    """Identify pointer slots that dyld will relocate, before moving array entries."""
    commands = macho(data)[0]
    segments = [struct.unpack_from('<QQQQ', data, p + 24) for c, p, _ in commands if c == 0x19]
    p = next(p for c, p, _ in commands if c == 0x80000022)
    start, size = struct.unpack_from('<II', data, p + 8)
    stream = data[start:start + size]
    cursor, segment, rel = 0, 0, 0
    slots = set()

    def uleb():
        nonlocal cursor
        value, shift = 0, 0
        while True:
            byte = stream[cursor]
            cursor += 1
            value |= (byte & 127) << shift
            if not byte & 128:
                return value
            shift += 7
            require(shift < 64, 'Invalid rebase ULEB')

    def apply(count, skip=0):
        nonlocal rel
        for _ in range(count):
            slots.add(segments[segment][2] + rel)
            rel += 8 + skip

    while cursor < len(stream):
        byte = stream[cursor]
        cursor += 1
        op, imm = byte & 0xf0, byte & 15
        if op == 0:
            break
        if op == 0x10:
            require(imm == 1, 'Expected pointer rebases')
        elif op == 0x20:
            segment, rel = imm, uleb()
        elif op == 0x30:
            rel += uleb()
        elif op == 0x40:
            rel += imm * 8
        elif op == 0x50:
            apply(imm)
        elif op == 0x60:
            apply(uleb())
        elif op == 0x70:
            apply(1, uleb())
        elif op == 0x80:
            count, skip = uleb(), uleb()
            apply(count, skip)
        else:
            raise ValueError('Unsupported rebase opcode')
    return slots


def prepare_image(raw):
    require(hashlib.sha256(raw).hexdigest() == SOURCE_HASH, 'Unexpected SDK donor fingerprint')
    data = bytearray(raw)
    secs = sections(data)
    relocations = rebase_slots(data)
    removed = []
    for name in ['__mod_init_func', '__objc_classlist']:
        _, size, p, header = secs[name]
        addresses = list(struct.unpack_from('<' + 'Q' * (size // 8), data, p))
        if name == '__mod_init_func':
            require(addresses == [0xe9b0, 0x369598, 0x3e2454], 'Unexpected constructor table')
            kept = addresses[1:]
            removed.append('_ABInstall')
        else:
            names = [class_info(data, a)[0] for a in addresses]
            require(names[:2] == ['ABLoginCoordinator', 'ABBootstrap'], 'Unexpected app bootstrap classes')
            kept = addresses[2:]
            removed.extend(names[:2])
        require(all(p + i * 8 in relocations for i in range(len(addresses))),
                'Moved pointers are not uniformly rebased')
        # Keep the now-unused tail unchanged: its rebase entries still exist.
        struct.pack_into('<' + 'Q' * len(kept), data, p, *kept)
        struct.pack_into('<Q', data, header + 40, len(kept) * 8)
    renames = {'Reachability': 'GBReachableX', 'QQApiAdItem': 'GBApiAdItem'}
    _, size, p, _ = sections(data)['__objc_classlist']
    for a in struct.unpack_from('<' + 'Q' * (size // 8), data, p):
        name, pos = class_info(data, a)
        if name in renames:
            new = renames[name].encode()
            require(len(new) == len(name), 'Class rename changes string length')
            data[pos:pos + len(new)] = new
    # Replace LC_ID_DYLIB within existing header space; executable offsets stay fixed.
    commands, first, _, _ = macho(data)
    replacement = []
    for cmd, pos, size in commands:
        if cmd == 0xd:
            path = INSTALL_NAME.encode() + b'\0'
            span = (24 + len(path) + 7) & ~7
            replacement.append((struct.pack('<IIIIII', 0xd, span, 24, 0, 0x10000, 0x10000) + path).ljust(span, b'\0'))
        else:
            replacement.append(bytes(data[pos:pos + size]))
    end = 32 + struct.unpack_from('<I', data, 20)[0]
    new_commands = b''.join(replacement)
    require(32 + len(new_commands) + 16 <= first, 'Insufficient header room')
    require(not any(data[end:32 + len(new_commands)]), 'Nonempty header growth area')
    data[32:32 + len(new_commands)] = new_commands
    struct.pack_into('<I', data, 20, len(new_commands))
    identity = uuid.UUID(bytes=hashlib.sha256(data).digest()[:16], version=4)
    for cmd, pos, _ in macho(data)[0]:
        if cmd == 0x1b:
            data[pos + 8:pos + 24] = identity.bytes
    # No instruction bytes are edited, including the dormant bootstrap code.
    _, size, p, _ = secs['__text']
    require(data[p:p + size] == raw[p:p + size], 'Unexpected machine-code edit')
    require_signing_room(data)
    return bytes(data), {'removed_startup_and_registration': removed, 'renamed_classes': renames,
                         'uuid': str(identity), 'dormant_game_specific_code_remains': True,
                         'sdk_runtime_tested': False}


def prepare_sdk():
    donor = local_path('sdk_donor_relative', 'inputs/payload.zip')
    out = ROOT / 'build/bili-candidate'
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(donor) as z:
        data, report = prepare_image(z.read(SDK_ROOT + 'Frameworks/ABLoginBridge.dylib'))
        framework = out / 'Frameworks/GBBiliSDK.framework'
        framework.mkdir(parents=True, exist_ok=True)
        (framework / 'GBBiliSDK').write_bytes(data)
        (framework / 'Info.plist').write_bytes(plistlib.dumps({
            'CFBundleExecutable': 'GBBiliSDK', 'CFBundleIdentifier': 'dev.local.GBBiliSDK',
            'CFBundleName': 'GBBiliSDK', 'CFBundlePackageType': 'FMWK',
            'CFBundleVersion': '0.8', 'CFBundleShortVersionString': '0.8',
            'MinimumOSVersion': '13.0', 'CFBundleSupportedPlatforms': ['iPhoneOS'],
        }, fmt=plistlib.FMT_BINARY))
        resources = ['BLGameSdkModel.momd/', 'BLImages.bundle/', 'GSCSDKResources.bundle/',
                     'GSCSVProgressHUD.bundle/', 'SDKImages.bundle/', 'WeiboSDK.bundle/']
        for entry in z.infolist():
            relative = entry.filename.removeprefix(SDK_ROOT)
            if not any(relative.startswith(prefix) for prefix in resources) and relative != 'countryList.plist':
                continue
            target = out / relative
            require(target.resolve().is_relative_to(out.resolve()), 'Unsafe resource path')
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(z.read(entry))
    report['source_sha256'] = SOURCE_HASH
    report['candidate_sha256'] = hashlib.sha256(data).hexdigest()
    (out / 'sdk-candidate.report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return out


if __name__ == '__main__':
    print(prepare_sdk())
