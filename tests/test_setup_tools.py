import gzip
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile


spec = importlib.util.spec_from_file_location("setup_tools", Path(__file__).resolve().parents[1] / "scripts/setup-tools.py")
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


class HelperTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.destination = Path(directory.name) / "helper"

    def asset(self, data, name="helper"):
        return {"name": name, "browser_download_url": "https://example.test/helper", "digest": "sha256:" + hashlib.sha256(data).hexdigest()}

    def test_verified_download(self):
        with patch.object(setup.urllib.request, "urlopen", return_value=io.BytesIO(b"helper")):
            setup.download(self.asset(b"helper"), self.destination)
        self.assertEqual(self.destination.read_bytes(), b"helper")

    def test_corrupt_download_preserves_installed_helper(self):
        self.destination.write_bytes(b"previous")
        info = {"tag_name": "test", "assets": [self.asset(b"expected")]}
        with patch.object(setup.urllib.request, "urlopen", return_value=io.BytesIO(b"corrupt")):
            with self.assertRaisesRegex(RuntimeError, "Checksum mismatch"):
                setup.install_asset(info, "helper", self.destination)
        self.assertEqual(self.destination.read_bytes(), b"previous")

    def test_missing_checksum_rejected_before_download(self):
        asset = self.asset(b"helper")
        asset.pop("digest")
        with patch.object(setup.urllib.request, "urlopen") as request:
            with self.assertRaisesRegex(RuntimeError, "Missing SHA-256"):
                setup.download(asset, self.destination)
            request.assert_not_called()

    def test_compressed_helpers(self):
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w") as package:
            package.writestr("deno", b"helper")
            package.writestr("../outside", b"unused")
        for name, data, member in (("ffmpeg.gz", gzip.compress(b"helper"), None), ("deno.zip", archive.getvalue(), "deno")):
            with self.subTest(name=name), patch.object(setup.urllib.request, "urlopen", return_value=io.BytesIO(data)):
                setup.install_asset({"tag_name": "test", "assets": [self.asset(data, name)]}, name, self.destination, member)
                self.assertEqual(self.destination.read_bytes(), b"helper")
        self.assertEqual(list(self.destination.parent.iterdir()), [self.destination])
