import copy
import json
from pathlib import Path
import unittest

from build_catalog_pairing import verified_profile as _verified_profile


def verified_profile(proof):
    return _verified_profile(proof, Path(__file__).resolve().parent / 'tests/fixtures/catalog_profile.h')


class Proof:
    def __init__(self, report):
        self.report = report

    def read_text(self, **kwargs):
        return json.dumps(self.report)


class CatalogProofTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parent / 'tests/fixtures/catalog_proof.json'
        cls.report = json.loads(path.read_text(encoding='utf-8'))

    def test_successful_evidence_matches_current_publication_header(self):
        self.assertEqual(verified_profile(Proof(self.report))['state'], 'compatible_metadata_candidate')

    def test_failed_anonymous_control_is_rejected(self):
        report = copy.deepcopy(self.report)
        report['cases'][1]['retcode'] = 1
        with self.assertRaisesRegex(ValueError, 'anonymous metadata controls'):
            verified_profile(Proof(report))

    def test_resource_mismatch_is_rejected_even_if_summary_flag_is_true(self):
        report = copy.deepcopy(self.report)
        report['cases'][2]['resource_fingerprints']['8']['sha256'] = 'mismatch'
        with self.assertRaisesRegex(ValueError, 'Resource fingerprints'):
            verified_profile(Proof(report))

    def test_seed_header_mismatch_is_rejected(self):
        report = copy.deepcopy(self.report)
        report['runs'][0]['seed_sha256'] = report['runs'][1]['seed_sha256'] = 'mismatch'
        with self.assertRaisesRegex(ValueError, 'header/proof mismatch'):
            verified_profile(Proof(report))


if __name__ == '__main__':
    unittest.main()
