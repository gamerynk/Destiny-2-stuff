import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "private_config", Path(__file__).resolve().parents[1] / "scripts/configure_private.py")
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)


class PrivateConfigTests(unittest.TestCase):
    def test_client_endpoints_and_both_module_locations(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = config.configure("client", directory, "10.0.0.10")
            self.assertEqual(len(paths), 2)
            self.assertEqual(paths[0].read_bytes(), paths[1].read_bytes())
            value = json.loads(paths[0].read_text())
            self.assertEqual(value["client"]["external_server"]["port_routes"], [[30975, "10.0.0.10", 30977]])
            self.assertEqual(value["server"]["bap_advertised_address"], "10.0.0.10")
            self.assertEqual(value["server"]["bap_advertised_port"], 30974)
            self.assertEqual(value["server"]["gameplay"]["transport_address"], "10.0.0.10")
            self.assertEqual(value["server"]["gameplay"]["bind_address"], "127.0.0.1")
            self.assertEqual(value["server"]["gameplay"]["topology"], "embedded")

    def test_server_cache_identity_and_interface(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "Restoration/cache/build_data.bin"
            cache.parent.mkdir(parents=True)
            cache.write_bytes(struct.pack("<8sIIIQ", b"RESTBLD1", 44, 1, 2, 0x1234))
            paths = config.configure("server", root, "10.0.0.10")
            self.assertEqual(len(paths), 1)
            settings = json.loads(paths[0].read_text())
            self.assertEqual(settings["server"]["build_data_equipment_hash"], "0x0000000000001234")
            self.assertEqual(settings["server"]["gameplay"]["bind_address"], "10.0.0.10")

    def test_existing_second_settings_file_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            existing = root / "bin/x64/Restoration/settings.json"
            existing.parent.mkdir(parents=True)
            existing.write_text("existing user preferences")
            with self.assertRaises(FileExistsError):
                config.configure("client", root, "127.0.0.1")
            self.assertEqual(existing.read_text(), "existing user preferences")
            self.assertFalse((root / "Restoration/settings.json").exists())

    def test_bad_cache_writes_no_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "invalid.bin"
            for data in (b"bad", struct.pack("<8sIIIQ", b"RESTBLD1", 43, 1, 2, 3)):
                cache.write_bytes(data)
                with self.assertRaises(ValueError):
                    config.configure("server", root, "10.0.0.10", cache)
                self.assertFalse((root / "Restoration/settings.json").exists())

    def test_bad_endpoints_rejected(self):
        for address in ("0.0.0.0", "224.0.0.1", "255.255.255.255", "localhost", "10.1", "::1", "999.1.2.3"):
            with self.subTest(address=address), self.assertRaises(ValueError):
                config.make_settings("client", address)


if __name__ == "__main__":
    unittest.main()
