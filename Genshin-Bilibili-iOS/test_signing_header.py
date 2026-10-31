"""Regression checks against a small v0.1 library fixture and rebuilt library."""
import plistlib
import struct
import unittest
import zipfile

from build_diagnostic import (APP, LIBRARY, PAGE, ROOT, bind_table_sizes, build_library, framework_info, macho,
                              normalize_library, require_signing_room)


class SigningHeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rebuilt = build_library().read_bytes()

    def test_original_failure_is_reproduced(self):
        data = (ROOT.parent / 'private/no-signature-padding.dylib').read_bytes()
        with self.assertRaisesRegex(ValueError, 'No safe header padding'):
            require_signing_room(data)

    def test_rebuilt_library_has_reserved_space(self):
        self.assertGreaterEqual(require_signing_room(self.rebuilt), 0x1000)

    def test_nonempty_space_is_not_treated_as_padding(self):
        data = bytearray(self.rebuilt)
        end = 32 + struct.unpack_from('<I', data, 20)[0]
        data[end] = 1
        with self.assertRaisesRegex(ValueError, 'No safe header padding'):
            require_signing_room(data)

    def test_existing_framework_signature_is_accepted(self):
        from local_config import local_path
        source = local_path('source_ipa_relative', 'inputs/original.ipa')
        with zipfile.ZipFile(source) as archive:
            data = archive.read(APP + 'Frameworks/AstrolabeCommon.framework/AstrolabeCommon')
        require_signing_room(data)

    def test_framework_metadata_matches_load_path(self):
        info = plistlib.loads(framework_info())
        self.assertEqual(info['CFBundlePackageType'], 'FMWK')
        self.assertTrue(LIBRARY.endswith('/GBDiagnostic.framework/' + info['CFBundleExecutable']))

    def test_segments_are_page_sized_except_linkedit_file_size(self):
        commands = macho(self.rebuilt)[0]
        segments = [struct.unpack_from('<16xQQQQ', self.rebuilt, pos + 8) for cmd, pos, _ in commands if cmd == 0x19]
        for vmaddr, vmsize, fileoff, filesize in segments[:-1]:
            self.assertEqual(vmsize % PAGE, 0)
            self.assertEqual(filesize, vmsize)
        self.assertEqual(segments[-1][1] % PAGE, 0)
        self.assertEqual(segments[-1][2] + segments[-1][3], len(self.rebuilt))

    def test_library_has_only_regular_binds(self):
        regular, lazy = bind_table_sizes(self.rebuilt)
        self.assertGreater(regular, 0)
        self.assertEqual(lazy, 0)

    def test_dylib_has_no_dylinker_command(self):
        self.assertNotIn(0xe, [cmd for cmd, _, _ in macho(self.rebuilt)[0]])

    def test_nonempty_segment_gap_is_rejected(self):
        data = bytearray(self.rebuilt)
        commands = macho(data)[0]
        text = next(pos for cmd, pos, _ in commands if cmd == 0x19)
        struct.pack_into('<Q', data, text + 48, 0x1000)
        data[0x1000] = 1
        with self.assertRaisesRegex(ValueError, 'Segment gap is not empty'):
            normalize_library(data)

    def test_install_name_matches_framework_layout(self):
        self.assertIn(b'@rpath/GBDiagnostic.framework/GBDiagnostic\0', self.rebuilt)


if __name__ == '__main__':
    unittest.main()
