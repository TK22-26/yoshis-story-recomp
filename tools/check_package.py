"""Enforce the Windows release allowlist: programs and documentation only."""
import argparse
from pathlib import Path
import zipfile

PROGRAMS = {"yoshis_story_recomp.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll",
            "concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
            "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "vccorlib140.dll",
            "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140_threads.dll"}


def document_names(root):
    documents = {"README.md", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md", "START HERE.txt"}
    documents.update("licenses/" + p.name for p in (root / "licenses").glob("*.txt"))
    return documents


def validate_package(path, root=None):
    root = root or Path(__file__).resolve().parent.parent
    documents = document_names(root)
    required = PROGRAMS | documents
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        files = [entry.filename for entry in entries if not entry.is_dir()]
        if len(files) != len(set(files)):
            raise ValueError("Package contains duplicate paths")
        unexpected = set(files) - required
        missing = required - set(files)
        if unexpected or missing:
            raise ValueError(f"Package allowlist failed: unexpected={sorted(unexpected)}, missing={sorted(missing)}")
        for entry in entries:
            if entry.is_dir():
                if entry.filename not in {"data/", "licenses/"}:
                    raise ValueError(f"Unexpected directory: {entry.filename}")
            elif entry.filename in documents:
                data = archive.read(entry)
                # Git's Windows checkout may use CRLF; document content must
                # still match. The inventory hashes the exact packaged bytes.
                if data.replace(b"\r\n", b"\n") != (root / entry.filename).read_bytes().replace(b"\r\n", b"\n"):
                    raise ValueError(f"Documentation differs from source: {entry.filename}")
            elif archive.read(entry)[:2] != b"MZ":
                raise ValueError(f"Invalid Windows program: {entry.filename}")
        if "data/" not in archive.namelist():
            raise ValueError("Missing empty data directory")
        if archive.testzip() is not None:
            raise ValueError("ZIP integrity check failed")
    return {"programs": len(PROGRAMS), "documents": len(documents)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    counts = validate_package(args.archive)
    print(f"Package verified: {counts['programs']} programs, {counts['documents']} documents, empty data directory.")


if __name__ == "__main__":
    main()
