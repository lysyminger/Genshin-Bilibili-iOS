"""Fixed call-site diagnostic for the supplied UUID; no runtime code writes."""
from local_config import local_path
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
BASE = 0x100000000
STUB = 0x1137c4100
ORIGINAL = 0x10f872948
DLSYM = 0x1129c3854
SITES = {0x10e941efc: 0x943cc293, 0x10e941f84: 0x143cc271}


def branch(source, target, link=False):
    delta = target - source
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('Branch is unaligned or out of ARM64 range')
    return (0x94000000 if link else 0x14000000) | ((delta >> 2) & 0x3ffffff)


def build_stub():
    from build_diagnostic import macho
    output = ROOT / 'build/GBURLWriterStub.o'
    output.parent.mkdir(exist_ok=True)
    subprocess.run([str(local_path('zig_relative', '.tools/zig13/ziglang/zig.exe')), 'cc',
        '-target', 'aarch64-ios.13.0', '-c', str(ROOT / 'src/GBURLWriterStub.S'),
        '-o', str(output)], check=True)
    data = output.read_bytes()
    code = None
    symbols = {}
    for command, pos, _ in macho(data)[0]:
        if command == 0x19:
            for i in range(struct.unpack_from('<I', data, pos + 64)[0]):
                section = pos + 72 + 80 * i
                if data[section:section + 16].rstrip(b'\0') == b'__text':
                    _, size, offset = struct.unpack_from('<QQI', data, section + 32)
                    if struct.unpack_from('<I', data, section + 60)[0]:
                        raise ValueError('Stub contains unresolved relocations')
                    code = bytearray(data[offset:offset + size])
        if command == 2:
            offset, count, strings, _ = struct.unpack_from('<IIII', data, pos + 8)
            for i in range(count):
                name, _, _, _, value = struct.unpack_from('<IBBHQ', data, offset + 16 * i)
                start = strings + name
                label = data[start:data.index(0, start)].decode()
                symbols[label] = value
    if code is None or len(code) > 512 or symbols.get('_GBURLWriterStub') != 0:
        raise ValueError('Unexpected stub layout')
    for label, target, link in [('_GBWriterDlsymCall', DLSYM, True),
                                 ('_GBWriterOriginalJump', ORIGINAL, False)]:
        offset = symbols[label]
        struct.pack_into('<I', code, offset, branch(STUB + offset, target, link))
    return bytes(code)


def patch_writer(data, stub):
    from build_diagnostic import UUID, macho, require
    commands, _, identity, _ = macho(data)
    require(identity == UUID, 'Unsupported URL writer executable')
    require(0 < len(stub) <= 512, 'Invalid URL writer stub size')
    text = None
    sections = []
    for command, pos, _ in commands:
        if command != 0x19 or data[pos + 8:pos + 24].rstrip(b'\0') != b'__TEXT':
            continue
        vm, _, offset, size = struct.unpack_from('<QQQQ', data, pos + 24)
        require(struct.unpack_from('<i', data, pos + 60)[0] & 4, '__TEXT is not executable')
        text = (vm, offset, size)
        for i in range(struct.unpack_from('<I', data, pos + 64)[0]):
            section = pos + 72 + 80 * i
            address, span = struct.unpack_from('<QQ', data, section + 32)
            sections.append((address, address + span))
    require(text is not None and text[0] == BASE, 'Unexpected image base')
    def file_offset(address, size):
        require(text[0] <= address and address + size <= text[0] + text[2], 'Patch outside __TEXT')
        return text[1] + address - text[0]
    target = file_offset(STUB, len(stub))
    require(not any(a < STUB + len(stub) and STUB < b for a, b in sections), 'Stub overlaps a section')
    require(not any(data[target:target + len(stub)]), 'Stub padding is not empty')
    require(data[file_offset(0x10e9bb78c, 8):file_offset(0x10e9bb78c, 8) + 8] ==
            bytes.fromhex('1ff500141cf50014'), 'String accessor fingerprint mismatch')
    for address, expected in SITES.items():
        require(struct.unpack_from('<I', data, file_offset(address, 4))[0] == expected,
                'URL writer call-site fingerprint mismatch')
    edited = bytearray(data)
    edited[target:target + len(stub)] = stub
    ranges = [(target, target + len(stub))]
    for address, expected in SITES.items():
        offset = file_offset(address, 4)
        struct.pack_into('<I', edited, offset, branch(address, STUB, bool(expected & 0x80000000)))
        ranges.append((offset, offset + 4))
    last = 0
    for first, end in sorted(ranges):
        require(edited[last:first] == data[last:first], 'Unrelated executable bytes changed')
        last = end
    require(edited[last:] == data[last:], 'Executable suffix changed')
    return bytes(edited), {'enabled': True, 'runtime_verified': False, 'seed_changed': False,
        'call_sites': [hex(a) for a in SITES], 'stub_vmaddr': hex(STUB), 'stub_bytes': len(stub),
        'original_setter_vmaddr': hex(ORIGINAL), 'runtime_executable_memory_writes': False}
