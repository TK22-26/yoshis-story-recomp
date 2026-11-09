"""Prepare or verify a maintainer-built Windows release without uploading a ROM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

from check_package import PROGRAMS, validate_package

ROOT = Path(__file__).resolve().parent.parent
REPOSITORY = "TK22-26/yoshis-story-recomp"


def command(*args, cwd=ROOT, env=None):
    return subprocess.check_output(args, cwd=cwd, env=env, text=True).strip()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def dependencies():
    runtime = re.search(r"\$runtimeCommit = '([0-9a-f]{40})'",
                        (ROOT / "tools/build_windows.ps1").read_text(encoding="utf-8"))[1]
    rt64 = command("git", "rev-parse", "HEAD:rt64")
    return {name: {"commit": commit, "patches": {
        p.relative_to(ROOT).as_posix(): digest(p.read_bytes().replace(b"\r\n", b"\n"))
        for p in sorted((ROOT / "patches" / folder).glob("*.patch"))}}
        for name, commit, folder in [("RT64", rt64, "rt64"), ("N64ModernRuntime", runtime, "runtime")]}


def metadata(tag, require_tag=False):
    version = re.search(r"project\(yoshis_story_recomp VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)",
                        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))[1]
    if tag != "windows-v" + version:
        raise ValueError("Windows release tag must match CMake version: windows-v" + version)
    source = command("git", "rev-parse", "HEAD")
    if command("git", "status", "--porcelain", "--untracked-files=normal", "--ignore-submodules=dirty"):
        raise ValueError("Release source must be committed and clean")
    subprocess.run(["git", "merge-base", "--is-ancestor", source, "origin/main"], cwd=ROOT, check=True)
    if require_tag and command("git", "rev-parse", "refs/tags/" + tag + "^{commit}") != source:
        raise ValueError("Release tag does not identify this source revision")
    return {"version": version, "tag": tag, "source": source, "dependencies": dependencies()}


def check_dependency(path, expected):
    if command("git", "rev-parse", "HEAD", cwd=path) != expected["commit"]:
        raise ValueError("Dependency revision mismatch: " + path.name)
    # Reconstruct the committed patch stack in a temporary index. This leaves
    # the actual checkout/index untouched and rejects extra local code changes.
    with tempfile.TemporaryDirectory(prefix="yoshi-release-index-") as temp:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(temp) / "index"))
        subprocess.run(["git", "read-tree", "HEAD"], cwd=path, env=env, check=True)
        for patch in expected["patches"]:
            subprocess.run(["git", "apply", "--cached", "--ignore-space-change", "--whitespace=nowarn"],
                           input=(ROOT / patch).read_bytes().replace(b"\r\n", b"\n"),
                           cwd=path, env=env, check=True)
        tree = command("git", "write-tree", cwd=path, env=env)
    subprocess.run(["git", "diff", "--exit-code", "--quiet", "--ignore-submodules=none", tree], cwd=path, check=True)
    if command("git", "ls-files", "--others", "--exclude-standard", cwd=path):
        raise ValueError("Untracked dependency files: " + path.name)


def check_desktop_executable(path):
    # A console subsystem accidentally shipped in 1.0.0. Check the actual PE,
    # and Windows' ability to extract the approved egg, before publishing again.
    import ctypes
    import struct
    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if data[offset:offset + 4] != b"PE\0\0" or struct.unpack_from("<H", data, offset + 24 + 68)[0] != 2:
        raise ValueError("The application must use the Windows GUI subsystem")
    if (ROOT / "assets/icon.png").read_bytes() not in data:
        raise ValueError("The application does not embed the approved egg icon")
    extract = ctypes.WinDLL("shell32").ExtractIconExW
    extract.argtypes = [ctypes.c_wchar_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
    extract.restype = ctypes.c_uint
    if not extract(str(path), -1, None, None, 0):
        raise ValueError("Windows cannot extract an icon from the application")


def build_info(archive_path):
    # Extract only the verified executable/runtime names, never data or an
    # arbitrary ZIP path. This command exits before any ROM/game initialization.
    with tempfile.TemporaryDirectory(prefix="yoshi-release-info-") as temp:
        with zipfile.ZipFile(archive_path) as archive:
            for name in PROGRAMS:
                (Path(temp) / name).write_bytes(archive.read(name))
        executable = Path(temp) / "yoshis_story_recomp.exe"
        check_desktop_executable(executable)
        process = subprocess.run([str(executable), "--build-info"],
                                 cwd=temp, capture_output=True, text=True, timeout=30, check=True)
        return json.loads(process.stdout)


def paths(output, version):
    stem = output / ("Yoshis-Story-" + version + "-windows-x64")
    return [Path(str(stem) + suffix) for suffix in (".zip", ".inventory.json", ".sha256")]


def verify(output, expected):
    archive_path, inventory_path, checksum_path = paths(output, expected["version"])
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    if inventory.get("schema") != 1 or any(inventory.get(k) != v for k, v in expected.items()):
        raise ValueError("Inventory source, version, tag or dependencies differ from this release")
    archive_hash = digest(archive_path.read_bytes())
    if (inventory.get("archive") != archive_path.name or inventory.get("sha256") != archive_hash or
            inventory.get("bytes") != archive_path.stat().st_size or
            checksum_path.read_text(encoding="ascii").strip() != archive_hash + "  " + archive_path.name):
        raise ValueError("Archive checksum or identity mismatch")
    validate_package(archive_path)
    with zipfile.ZipFile(archive_path) as archive:
        members = {i.filename: {"sha256": digest(archive.read(i)), "bytes": i.file_size}
                   for i in archive.infolist() if not i.is_dir()}
    if members != inventory.get("members"):
        raise ValueError("Package member hashes differ from the inventory")
    info = build_info(archive_path)
    if info != {"schema": 1, "version": expected["version"], "source": expected["source"], "dirty": False}:
        raise ValueError("The executable was not built cleanly from this exact release revision")
    return paths(output, expected["version"])


def prepare(source_archive, output, expected, runtime):
    check_dependency(ROOT / "rt64", expected["dependencies"]["RT64"])
    check_dependency(runtime, expected["dependencies"]["N64ModernRuntime"])
    validate_package(source_archive)
    artifacts = paths(output, expected["version"])
    if any(p.exists() for p in artifacts):
        raise ValueError("Preserve existing release artifacts; choose a new output directory")
    output.mkdir(parents=True, exist_ok=True)
    archive_path, inventory_path, checksum_path = artifacts
    shutil.copyfile(source_archive, archive_path)
    with zipfile.ZipFile(archive_path) as archive:
        members = {i.filename: {"sha256": digest(archive.read(i)), "bytes": i.file_size}
                   for i in archive.infolist() if not i.is_dir()}
    archive_hash = digest(archive_path.read_bytes())
    inventory = dict(expected, schema=1, archive=archive_path.name, sha256=archive_hash,
                     bytes=archive_path.stat().st_size, members=members)
    inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    checksum_path.write_text(archive_hash + "  " + archive_path.name + "\n", encoding="ascii")
    return verify(output, expected)


def publish(artifacts, expected):
    release = json.loads(command("gh", "release", "view", expected["tag"], "--repo", REPOSITORY,
                                 "--json", "isDraft,tagName,assets,targetCommitish,url"))
    if (not release["isDraft"] or release["tagName"] != expected["tag"] or
            {a["name"] for a in release["assets"]} != {p.name for p in artifacts}):
        raise ValueError("Expected an unpublished draft with exactly the verified release assets")
    local = {p.name: p for p in artifacts}
    for asset in release["assets"]:
        path = local[asset["name"]]
        if asset.get("digest") != "sha256:" + digest(path.read_bytes()) or asset["size"] != path.stat().st_size:
            raise ValueError("Remote draft asset differs from the verified local file: " + path.name)
    subprocess.run(["gh", "release", "edit", expected["tag"], "--repo", REPOSITORY,
                    "--draft=false", "--prerelease=false", "--latest"], cwd=ROOT, check=True)
    return release["url"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("check", "prepare", "verify", "publish"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--runtime", type=Path, default=ROOT / ".deps/N64ModernRuntime")
    parser.add_argument("--require-tag", action="store_true")
    args = parser.parse_args()
    expected = metadata(args.tag, args.require_tag or args.action == "publish")
    if args.action == "check":
        print(json.dumps(expected))
        return
    if not args.output:
        parser.error("prepare, verify and publish require --output")
    if args.action == "prepare":
        if not args.archive:
            parser.error("prepare requires --archive")
        artifacts = prepare(args.archive, args.output, expected, args.runtime)
    else:
        artifacts = verify(args.output, expected)
    if args.action == "publish":
        print(publish(artifacts, expected))
    else:
        print(json.dumps({"verified": True, "source": expected["source"], "assets": [str(p) for p in artifacts]}))


if __name__ == "__main__":
    main()
