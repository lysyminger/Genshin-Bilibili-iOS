"""Exercise the actual ARM64 stub and fixed original executable guards."""
from pathlib import Path
from local_config import local_path
import struct
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.tools/python'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *
from url_writer_patch import ROOT, STUB, ORIGINAL, DLSYM, SITES, branch, build_stub, patch_writer


class URLWriterPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stub = build_stub()
        with zipfile.ZipFile(local_path('source_ipa_relative', 'inputs/original.ipa')) as archive:
            cls.original = archive.read('Payload/Yuanshen.app/Yuanshen')

    def test_branch_limits(self):
        for delta in [-134217732, 134217728, 2]:
            with self.assertRaises(ValueError):
                branch(0x100000000, 0x100000000 + delta)

    def test_original_fingerprints_and_patch(self):
        edited, report = patch_writer(self.original, self.stub)
        self.assertEqual(len(edited), len(self.original))
        self.assertFalse(report['seed_changed'])
        for address, expected in SITES.items():
            original = struct.unpack_from('<I', self.original, address - 0x100000000)[0]
            self.assertEqual(original, expected)
            self.assertEqual(struct.unpack_from('<I', edited, address - 0x100000000)[0],
                             branch(address, STUB, bool(expected & 0x80000000)))
        with self.assertRaisesRegex(ValueError, 'padding is not empty'):
            patch_writer(edited, self.stub)

    def test_changed_call_site_is_rejected(self):
        edited = bytearray(self.original)
        edited[next(iter(SITES)) - 0x100000000] ^= 1
        with self.assertRaisesRegex(ValueError, 'call-site fingerprint'):
            patch_writer(edited, self.stub)

    def test_stub_preserves_original_call_for_both_sites(self):
        regs = [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
                UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_X6, UC_ARM64_REG_X7, UC_ARM64_REG_X8]
        saved = [UC_ARM64_REG_X19, UC_ARM64_REG_X20, UC_ARM64_REG_X21, UC_ARM64_REG_X22,
                 UC_ARM64_REG_X23, UC_ARM64_REG_X24, UC_ARM64_REG_X25, UC_ARM64_REG_X26,
                 UC_ARM64_REG_X27, UC_ARM64_REG_X28, UC_ARM64_REG_X29]
        for site, original_word in SITES.items():
            for available in [False, True]:
                for result in [0, 31]:
                    with self.subTest(site=hex(site), available=available, result=result):
                        emu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
                        callback, sentinel, stack = 0x200000000, 0x200001000, 0x300000000
                        for page in {STUB & ~4095, DLSYM & ~4095, ORIGINAL & ~4095,
                                     site & ~4095, callback, sentinel}:
                            emu.mem_map(page, 4096)
                        emu.mem_map(stack, 65536)
                        emu.mem_write(STUB, self.stub)
                        emu.mem_write(site, struct.pack('<I', branch(site, STUB, bool(original_word & 0x80000000))))
                        sp = stack + 65536 - 256
                        emu.reg_write(UC_ARM64_REG_SP, sp)
                        emu.reg_write(UC_ARM64_REG_X30, sentinel)
                        for i, reg in enumerate(regs + saved):
                            emu.reg_write(reg, 0x123400 + i * 16)
                        expected = {reg: emu.reg_read(reg) for reg in regs + saved}
                        return_address = site + 4 if original_word & 0x80000000 else sentinel
                        observed = {'diagnostic': 0, 'original': 0}

                        def hook(cpu, address, size, user):
                            if address == DLSYM:
                                self.assertEqual(cpu.reg_read(UC_ARM64_REG_X0), (1 << 64) - 2)
                                name = bytes(cpu.mem_read(cpu.reg_read(UC_ARM64_REG_X1), 24)).split(b'\0')[0]
                                self.assertEqual(name, b'GBDispatchURLWriteTrace')
                                cpu.reg_write(UC_ARM64_REG_X0, callback if available else 0)
                                cpu.reg_write(UC_ARM64_REG_PC, cpu.reg_read(UC_ARM64_REG_X30))
                            elif address == callback:
                                observed['diagnostic'] += 1
                                self.assertEqual(cpu.reg_read(regs[0]), expected[regs[0]])
                                self.assertEqual(cpu.reg_read(regs[1]), expected[regs[1]])
                                self.assertEqual(cpu.reg_read(UC_ARM64_REG_SP) % 16, 0)
                                for i, reg in enumerate(regs):
                                    cpu.reg_write(reg, 0xdead0000 + i)
                                cpu.reg_write(UC_ARM64_REG_PC, cpu.reg_read(UC_ARM64_REG_X30))
                            elif address == ORIGINAL:
                                observed['original'] += 1
                                for reg, value in expected.items():
                                    self.assertEqual(cpu.reg_read(reg), value)
                                self.assertEqual(cpu.reg_read(UC_ARM64_REG_SP), sp)
                                self.assertEqual(cpu.reg_read(UC_ARM64_REG_X30), return_address)
                                cpu.reg_write(UC_ARM64_REG_X0, result)
                                cpu.reg_write(UC_ARM64_REG_PC, return_address)
                            elif address == return_address:
                                cpu.emu_stop()
                        emu.hook_add(UC_HOOK_CODE, hook)
                        emu.emu_start(site, 0, count=100)
                        self.assertEqual(observed, {'diagnostic': int(available), 'original': 1})
                        self.assertEqual(emu.reg_read(UC_ARM64_REG_X0), result)
                        self.assertEqual(emu.reg_read(UC_ARM64_REG_SP), sp)


if __name__ == '__main__':
    unittest.main()
