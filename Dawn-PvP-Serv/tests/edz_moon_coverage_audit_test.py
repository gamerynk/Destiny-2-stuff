import os
"""Validate shipped package metadata and distinguish category proof from quota estimates."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/content'))
from audit_edz_moon_coverage import audit


class CoverageAudit(unittest.TestCase):
    def test_exact_sources_do_not_imply_complete_patrol_policy(self):
        root = Path(__file__).resolve().parents[1]
        data = audit(root.parent / 'reference/production/Dawn-production', Path(os.environ['DAWN_GAME_ROOT']))
        for name, count, sectors in [('edz', 206, 16), ('moon', 204, 4)]:
            world = data['worlds'][name]
            self.assertEqual(world['sources'], count)
            self.assertEqual(world['origins']['bootstrap'], 1)
            self.assertEqual(len(world['lostSectorBubbles']), sectors)
            self.assertIn('unresolved', world['ordinaryCoverage'])
            self.assertIn('not a verified retail', world['quotaStatus'])
            for source in world['nativeSources']:
                self.assertEqual(source['installed']['resourceClass'], '80809C36')
                self.assertGreater(source['installed']['entryBytes'], 0)
                self.assertNotIn('quota', source)
        self.assertEqual(data['worlds']['moon']['categoryWidths'][8], 1)


if __name__ == '__main__':
    unittest.main()
