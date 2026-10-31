"""Ensure checks see staged secrets even when the working copy has been cleaned."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from check_publication import check, review_file


class PublicationTests(unittest.TestCase):
    def test_private_path_is_rejected(self):
        self.assertIn('private_or_generated_path', review_file('private/config.json', b'{}'))

    def test_known_local_value_is_rejected_without_echoing_it(self):
        findings = review_file('example.py', b'value="invented-private-value"', {b'invented-private-value'})
        self.assertEqual(findings, ['local_private_value'])

    def test_test_fixture_is_allowed(self):
        self.assertEqual(review_file('tests/fixture.h', b'const char *seed = "0123456789abcdef";'), [])

    def test_index_is_checked_instead_of_unstaged_clean_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            path = root / 'example.py'
            path.write_text('"ghp_' + 'X' * 36 + '"', encoding='utf-8')
            subprocess.run(['git', '-C', str(root), 'add', 'example.py'], check=True)
            path.write_text('# cleaned but not staged', encoding='utf-8')
            self.assertEqual(check(root)['findings'], [{'path': 'example.py', 'category': 'github_token'}])

    def test_force_added_private_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / '.gitignore').write_text('private/\n', encoding='utf-8')
            (root / 'private').mkdir()
            (root / 'private/config.json').write_text('{}', encoding='utf-8')
            subprocess.run(['git', '-C', str(root), 'add', '-f', 'private/config.json'], check=True)
            self.assertIn({'path': 'private/config.json', 'category': 'private_or_generated_path'}, check(root)['findings'])


if __name__ == '__main__':
    unittest.main()
