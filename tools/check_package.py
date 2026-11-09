"""Enforce the Windows release allowlist: programs and documentation only."""
import argparse
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    programs = {"yoshis_story_recomp.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll"}
    documents = {"README.md", "THIRD_PARTY_NOTICES.md"}
    documents.update("licenses/" + p.name for p in (root / "licenses").glob("*.txt"))
    required = programs | documents
    with zipfile.ZipFile(args.archive) as archive:
        entries = archive.infolist()
        files = [entry.filename for entry in entries if not entry.is_dir()]
        if len(files) != len(set(files)):
            raise SystemExit("Package contains duplicate paths")
        unexpected = set(files) - required
        missing = required - set(files)
        if unexpected or missing:
            raise SystemExit(f"Package allowlist failed: unexpected={sorted(unexpected)}, missing={sorted(missing)}")
        for entry in entries:
            if entry.is_dir():
                if entry.filename not in {"data/", "licenses/"}:
                    raise SystemExit(f"Unexpected directory: {entry.filename}")
            elif entry.filename in documents:
                data = archive.read(entry)
                if data != (root / entry.filename).read_bytes():
                    raise SystemExit(f"Documentation differs from source: {entry.filename}")
            elif archive.read(entry)[:2] != b"MZ":
                raise SystemExit(f"Invalid Windows program: {entry.filename}")
        if "data/" not in archive.namelist():
            raise SystemExit("Missing empty data directory")
    print(f"Package verified: {len(programs)} programs, {len(documents)} documents, empty data directory.")


if __name__ == "__main__":
    main()
