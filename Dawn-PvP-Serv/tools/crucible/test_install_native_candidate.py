import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import install_native_candidate as installer


class PairedInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'game'
        self.packages = Path(self.temp.name) / 'stage'
        self.packages.mkdir()
        for relative, data in {'destiny2.exe': b'fixture game', 'steam_api64.dll': b'old root DLL',
                               'bin/x64/steam_api64.dll': b'old bin DLL',
                               'packages/w64_fixture_0001_0.pkg': b'old package',
                               'Restoration/settings.json': b'preserve settings'}.items():
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        self.client = Path(self.temp.name) / 'client.dll'
        self.client.write_bytes(b'new client')
        package = self.packages / 'w64_fixture_0001_0.pkg'
        package.write_bytes(b'new package')
        self.manifest = {'status': 'staged_only', 'packages': [dict(package=package.name,
                         source_sha256=installer.digest(self.root / 'packages' / package.name),
                         staged_sha256=installer.digest(package))]}
        self.write_manifest()
        self.baseline = {str(p.relative_to(self.root)): p.read_bytes()
                         for p in self.root.rglob('*') if p.is_file()}

    def write_manifest(self):
        (self.packages / 'staging-receipt.json').write_text(json.dumps(self.manifest))

    def install(self):
        return installer.install(self.root, self.client, self.packages, check_process=False,
                                 game_sha256=installer.digest(self.root / 'destiny2.exe'))

    def assert_baseline(self):
        for relative, data in self.baseline.items():
            self.assertEqual((self.root / relative).read_bytes(), data)

    def test_install_and_restore_pair(self):
        receipt = self.install()
        self.assertEqual((self.root / 'steam_api64.dll').read_bytes(), b'new client')
        self.assertEqual((self.root / 'bin/x64/steam_api64.dll').read_bytes(), b'new client')
        self.assertEqual((self.root / 'packages/w64_fixture_0001_0.pkg').read_bytes(), b'new package')
        self.assertEqual((self.root / 'Restoration/settings.json').read_bytes(), b'preserve settings')
        self.assertEqual(json.loads(receipt.read_text())['status'], 'installed_pending_live_validation')
        installer.restore(receipt, check_process=False)
        self.assert_baseline()

    def test_failure_after_dlls_restores_all_originals(self):
        original = installer.replace_verified
        calls = 0

        def fail_once(*args):
            nonlocal calls
            calls += 1
            if calls == 3:
                raise OSError('simulated package replacement failure')
            return original(*args)

        with patch.object(installer, 'replace_verified', fail_once), self.assertRaises(OSError):
            self.install()
        self.assert_baseline()
        receipts = list(self.root.glob('Restoration/native-update-backups/*/installation.json'))
        self.assertEqual(len(receipts), 1)
        self.assertEqual(json.loads(receipts[0].read_text())['status'], 'restored')

    def test_reject_changed_preimage_before_writing(self):
        self.manifest['packages'][0]['source_sha256'] = '0' * 64
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.install()
        self.assert_baseline()
        self.assertFalse((self.root / 'Restoration/native-update-backups').exists())

    def test_restore_refuses_to_overwrite_later_update(self):
        receipt = self.install()
        target = self.root / 'steam_api64.dll'
        target.write_bytes(b'later independent client')
        with self.assertRaisesRegex(ValueError, 'changed after'):
            installer.restore(receipt, check_process=False)
        self.assertEqual(target.read_bytes(), b'later independent client')
        self.assertEqual((self.root / 'bin/x64/steam_api64.dll').read_bytes(), b'new client')

    def test_reject_path_escape(self):
        self.manifest['packages'][0]['package'] = '../outside.pkg'
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'filename'):
            self.install()
        self.assert_baseline()


if __name__ == '__main__':
    unittest.main()
