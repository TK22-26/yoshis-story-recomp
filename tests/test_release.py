"""ROM-free regressions for the release integrity boundary."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import check_package
import release


class ReleaseVerification(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        self.expected = {"version": "1.0.0", "tag": "windows-v1.0.0", "source": "a" * 40, "dependencies": {}}
        self.archive, self.inventory, self.checksum = release.paths(self.output, "1.0.0")
        self.files = {name: b"MZsynthetic test program" for name in check_package.PROGRAMS}
        self.files.update({name: (ROOT / name).read_bytes() for name in check_package.document_names(ROOT)})
        self.info = {"schema": 1, "version": "1.0.0", "source": "a" * 40, "dirty": False}
        self.write_bundle()

    def write_bundle(self):
        with zipfile.ZipFile(self.archive, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("data/", b"")
            for name, content in self.files.items():
                archive.writestr(name, content)
        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.manifest = dict(self.expected, schema=1, archive=self.archive.name, sha256=digest,
                             bytes=self.archive.stat().st_size,
                             members={n: {"sha256": hashlib.sha256(b).hexdigest(), "bytes": len(b)}
                                      for n, b in self.files.items()})
        self.inventory.write_text(json.dumps(self.manifest), encoding="utf-8")
        self.checksum.write_text(digest + "  " + self.archive.name + "\n", encoding="ascii")

    def verify(self):
        with patch.object(release, "build_info", return_value=self.info):
            return release.verify(self.output, self.expected)

    def test_valid_bundle(self):
        self.assertEqual(len(self.verify()), 3)

    def test_unapproved_file_rejected_even_with_matching_hashes(self):
        self.files["data/unapproved.bin"] = b"not permitted"
        self.write_bundle()
        with self.assertRaisesRegex(ValueError, "allowlist"):
            self.verify()

    def test_changed_member_rejected(self):
        self.manifest["members"]["SDL2.dll"]["sha256"] = "0" * 64
        self.inventory.write_text(json.dumps(self.manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "member hashes"):
            self.verify()

    def test_changed_archive_rejected(self):
        self.archive.write_bytes(self.archive.read_bytes() + b"modified")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.verify()

    def test_wrong_source_or_version_rejected(self):
        for field, value in [("source", "b" * 40), ("version", "9.9.9")]:
            with self.subTest(field=field):
                self.write_bundle()
                self.manifest[field] = value
                self.inventory.write_text(json.dumps(self.manifest), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Inventory source"):
                    self.verify()

    def test_dirty_or_stale_binary_rejected(self):
        for change in [{"dirty": True}, {"source": "b" * 40}, {"version": "0.1.0"}]:
            with self.subTest(change=change):
                self.info = dict(schema=1, version="1.0.0", source="a" * 40, dirty=False)
                self.info.update(change)
                with self.assertRaisesRegex(ValueError, "executable"):
                    self.verify()

    def test_document_content_must_match_source(self):
        self.files["README.md"] = b"Different release documentation"
        self.write_bundle()
        with self.assertRaisesRegex(ValueError, "Documentation differs"):
            self.verify()

    def test_document_line_endings_are_portable(self):
        for name in check_package.document_names(ROOT):
            self.files[name] = self.files[name].replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        self.write_bundle()
        self.verify()


if __name__ == "__main__":
    unittest.main()
