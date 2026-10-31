"""Read-only Mach-O/Objective-C inspection for the supplied local IPA and SDK."""
import argparse
import json
from pathlib import Path
import struct
import sys
import zipfile

sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.tools/python'))


class MachO:
    def __init__(self, data):
        self.data = data
        if data[:4] != bytes.fromhex('cffaedfe'):
            raise ValueError('Expected a little-endian 64-bit Mach-O')
        self.segments = []
        self.sections = {}
        self.labels = {}
        self.symtab = None
        self.dysymtab = None
        self.commands = []
        pos = 32
        for _ in range(self.unpack('<I', 16)[0]):
            cmd, size = self.unpack('<II', pos)
            if size < 8 or pos + size > len(data):
                raise ValueError('Invalid load command')
            self.commands.append((cmd, pos, size))
            if cmd == 0x19:
                vm, vm_size, offset, file_size = self.unpack('<QQQQ', pos + 24)
                self.segments.append((vm, vm_size, offset, file_size))
                for index in range(self.unpack('<I', pos + 64)[0]):
                    start = pos + 72 + index * 80
                    name = data[start:start + 16].rstrip(b'\0').decode()
                    segment = data[start + 16:start + 32].rstrip(b'\0').decode()
                    addr, span, offset = self.unpack('<QQI', start + 32)
                    flags, reserved1, reserved2 = self.unpack('<III', start + 64)
                    self.sections[(segment, name)] = (addr, span, offset, flags, reserved1, reserved2)
            if cmd == 2:
                self.symtab = self.unpack('<IIII', pos + 8)
            if cmd == 0xb:
                self.dysymtab = self.unpack('<' + 'I' * 18, pos + 8)
            pos += size
        self.classes = self.read_classes()
        self.read_symbols()
        self.read_objc_stubs()

    def read_objc_stubs(self):
        # Clang emits selector-specific 32-byte stubs instead of named symbols.
        for (_, name), (addr, size, offset, *_) in self.sections.items():
            if name != '__objc_stubs':
                continue
            for delta in range(0, size, 32):
                adrp, ldr = self.unpack('<II', offset + delta)
                if adrp & 0x9f00001f != 0x90000001 or ldr & 0xffc003ff != 0xf9400021:
                    continue
                imm = ((adrp >> 5) & 0x7ffff) << 2 | ((adrp >> 29) & 3)
                if imm & 0x100000:
                    imm -= 0x200000
                page = (addr + delta) & ~0xfff
                selref = page + (imm << 12) + ((ldr >> 10) & 0xfff) * 8
                self.labels[addr + delta] = 'objc_msgSend$' + self.string(self.pointer(selref))

    def unpack(self, fmt, offset):
        return struct.unpack_from(fmt, self.data, offset)

    def offset(self, addr):
        for vm, _, offset, size in self.segments:
            if vm <= addr < vm + size:
                return offset + addr - vm
        raise ValueError(f'Address outside file: {addr:#x}')

    def pointer(self, addr):
        return self.unpack('<Q', self.offset(addr))[0]

    def string(self, addr):
        pos = self.offset(addr)
        end = self.data.index(0, pos, pos + 4096)
        return self.data[pos:end].decode('utf-8', 'replace')

    def read_methods(self, addr):
        if not addr:
            return []
        flags, count = self.unpack('<II', self.offset(addr))
        if count > 10000:
            raise ValueError('Invalid method count')
        stride = flags & 0xffff
        small = bool(flags & 0x80000000)
        methods = []
        for index in range(count):
            entry = addr + 8 + index * stride
            if small:
                name_off, type_off, imp_off = self.unpack('<iii', self.offset(entry))
                name = entry + name_off
                if not flags & 0x40000000:
                    name = self.pointer(name)
                type_addr, imp = entry + 4 + type_off, entry + 8 + imp_off
            else:
                name, type_addr, imp = self.unpack('<QQQ', self.offset(entry))
            methods.append({'selector': self.string(name), 'types': self.string(type_addr), 'imp': imp})
        return methods

    def read_class(self, addr):
        isa, _, _, _, ro = self.unpack('<QQQQQ', self.offset(addr))
        ro &= ~7
        name, methods = self.unpack('<QQ', self.offset(ro) + 24)
        return self.string(name), self.read_methods(methods), isa

    def read_classes(self):
        result = {}
        for (_, name), (addr, size, offset, *_) in self.sections.items():
            if name != '__objc_classlist':
                continue
            for index in range(0, size, 8):
                class_addr = self.unpack('<Q', offset + index)[0]
                name, methods, isa = self.read_class(class_addr)
                class_methods = self.read_class(isa)[1]
                result[name] = {'address': class_addr, 'instance': methods, 'class': class_methods}
                self.labels[class_addr] = name
                self.labels[isa] = name + '.metaclass'
                for kind, entries in [('-', methods), ('+', class_methods)]:
                    for method in entries:
                        self.labels[method['imp']] = f'{kind}[{name} {method["selector"]}]'
        return result

    def read_symbols(self):
        if not self.symtab:
            return
        offset, count, string_offset, _ = self.symtab
        symbols = []
        for index in range(count):
            name, _, _, _, addr = self.unpack('<IBBHQ', offset + 16 * index)
            pos = string_offset + name
            label = self.data[pos:self.data.index(0, pos)].decode('utf-8', 'replace')
            symbols.append(label)
            if addr:
                self.labels.setdefault(addr, label)
        if not self.dysymtab:
            return
        indirect_offset, indirect_count = self.dysymtab[12:14]
        for (_, name), (addr, size, _, flags, first, stride) in self.sections.items():
            if flags & 0xff != 8 or not stride:
                continue
            for index in range(size // stride):
                symbol = self.unpack('<I', indirect_offset + 4 * (first + index))[0]
                if symbol < len(symbols):
                    self.labels[addr + index * stride] = symbols[symbol]

    def describe(self, addr):
        if addr in self.labels:
            return self.labels[addr]
        try:
            for (_, name), (start, size, *_) in self.sections.items():
                if start <= addr < start + size:
                    if name in ('__cstring', '__objc_methname', '__objc_classname'):
                        return repr(self.string(addr))
                    if name in ('__objc_selrefs', '__objc_classrefs', '__objc_superrefs', '__got'):
                        pointed = self.pointer(addr)
                        return self.labels.get(pointed) or repr(self.string(pointed))
                    if name == '__cfstring':
                        return repr(self.string(self.pointer(addr + 16)))
        except (ValueError, struct.error):
            pass
        return ''

    def disassemble(self, addr, size):
        from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
        from capstone.arm64 import ARM64_OP_IMM, ARM64_OP_MEM, ARM64_OP_REG
        decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        decoder.detail = True
        offset = self.offset(addr)
        regs = {}
        lines = []
        for ins in decoder.disasm(self.data[offset:offset + size], addr):
            ops = ins.operands
            annotation = ''
            if ins.mnemonic in ('adrp', 'adr') and len(ops) == 2:
                regs[ops[0].reg] = ops[1].imm
            elif ins.mnemonic == 'add' and len(ops) == 3 and ops[1].reg in regs and ops[2].type == ARM64_OP_IMM:
                regs[ops[0].reg] = regs[ops[1].reg] + ops[2].imm
                annotation = self.describe(regs[ops[0].reg])
            elif ins.mnemonic == 'ldr' and len(ops) == 2 and ops[1].type == ARM64_OP_MEM:
                base = regs.get(ops[1].mem.base)
                if base is not None:
                    target = base + ops[1].mem.disp
                    annotation = self.describe(target)
                    try:
                        regs[ops[0].reg] = self.pointer(target)
                    except (ValueError, struct.error):
                        regs.pop(ops[0].reg, None)
            elif ins.mnemonic in ('b', 'bl') and ops and ops[0].type == ARM64_OP_IMM:
                annotation = self.describe(ops[0].imm)
                if ins.mnemonic == 'bl':
                    for reg in list(regs):
                        if ins.reg_name(reg) in [f'x{i}' for i in range(18)]:
                            del regs[reg]
            elif ops and ops[0].type == ARM64_OP_REG and ins.mnemonic not in ('cmp', 'stp', 'str', 'cbz', 'cbnz', 'tbz', 'tbnz'):
                regs.pop(ops[0].reg, None)
            lines.append(f'{ins.address:#x}: {ins.mnemonic:8s} {ins.op_str}' + (f' ; {annotation}' if annotation else ''))
        return '\n'.join(lines)


def load_binary(kind):
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / 'Genshin-Bilibili-iOS'))
    from local_config import local_path
    if kind == 'genshin':
        archive = local_path('source_ipa_relative', 'inputs/original.ipa')
        member = 'Payload/Yuanshen.app/Yuanshen'
    else:
        archive = local_path('sdk_donor_relative', 'inputs/payload.zip')
        member = 'Payload/Arknights.app/Frameworks/ABLoginBridge.dylib'
    with zipfile.ZipFile(archive) as z:
        return MachO(z.read(member))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['genshin', 'arknights'])
    parser.add_argument('--classes', nargs='*')
    parser.add_argument('--address', type=lambda s: int(s, 0))
    parser.add_argument('--size', type=lambda s: int(s, 0), default=512)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    macho = load_binary(args.kind)
    if args.address is not None:
        result = macho.disassemble(args.address, args.size)
    else:
        selected = {k: v for k, v in macho.classes.items() if not args.classes or any(s.lower() in k.lower() for s in args.classes)}
        result = json.dumps(selected, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(result, encoding='utf-8')
        print(f'Wrote {args.output} ({len(result)} characters)')
    else:
        print(result)
