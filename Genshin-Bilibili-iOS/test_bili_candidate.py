import hashlib
import struct
import unittest
import zipfile

from prepare_bili_sdk import ROOT, SDK_ROOT, SOURCE_HASH, class_info, prepare_image, sections, macho


class BiliCandidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from local_config import local_path
        donor = local_path('sdk_donor_relative', 'inputs/payload.zip')
        with zipfile.ZipFile(donor) as z:
            cls.raw = z.read(SDK_ROOT + 'Frameworks/ABLoginBridge.dylib')
        cls.data, cls.report = prepare_image(cls.raw)

    def test_locked_donor(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(), SOURCE_HASH)
        with self.assertRaisesRegex(ValueError, 'fingerprint'):
            prepare_image(self.raw[:-1])

    def test_sdk_class_registration_and_no_ark_bootstrap(self):
        _, size, p, _ = sections(self.data)['__objc_classlist']
        names = [class_info(self.data, a)[0] for a in struct.unpack_from('<' + 'Q' * (size // 8), self.data, p)]
        self.assertEqual(len(names), 836)
        self.assertIn('GSSDK', names)
        self.assertIn('BLGameSdk', names)
        self.assertNotIn('ABBootstrap', names)
        self.assertNotIn('ABLoginCoordinator', names)
        self.assertNotIn('Reachability', names)
        self.assertNotIn('QQApiAdItem', names)
        self.assertIn('GBReachableX', names)
        self.assertIn('GBApiAdItem', names)

    def test_only_sdk_constructors_remain(self):
        _, size, p, _ = sections(self.data)['__mod_init_func']
        self.assertEqual(struct.unpack_from('<' + 'Q' * (size // 8), self.data, p),
                         (0x369598, 0x3e2454))

    def test_instructions_unchanged(self):
        _, size, p, _ = sections(self.raw)['__text']
        self.assertEqual(self.raw[p:p + size], self.data[p:p + size])

    def test_install_name_and_signing_space(self):
        commands, first, _, _ = macho(self.data)
        end = 32 + struct.unpack_from('<I', self.data, 20)[0]
        self.assertGreaterEqual(first - end, 16)
        names = []
        for cmd, p, size in commands:
            if cmd == 0xd:
                off = struct.unpack_from('<I', self.data, p + 8)[0]
                names.append(self.data[p + off:p + size].split(b'\0')[0])
        self.assertEqual(names, [b'@rpath/GBBiliSDK.framework/GBBiliSDK'])


if __name__ == '__main__':
    unittest.main()
