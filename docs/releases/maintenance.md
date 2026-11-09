# Preparing and publishing Windows releases

This follows the maintainer-built bundle process used by
[Jet Force Gemini Recomp](https://github.com/TK22-26/JetForceGemini-Recomp/blob/main/docs/maintenance/releases.md):
build with the private ROM locally, attach three verified files to a draft,
and validate the exact tagged revision in ROM-free CI.

Windows releases use `windows-v<version>` tags. The fork retains upstream tags;
never move an inherited Linux tag to a Windows commit.

## Prepare

1. Update the CMake version, changelog, README, setup guide, and release record.
   Commit and push the reviewed source to `main`.
2. Build and test from that clean revision using `tools/build_windows.ps1 -Package`.
   Its `--build-info` command reports the compiled source/version without a ROM
   or SDL initialization. Packaging includes only explicitly named programs and
   documentation; `data/` is created empty.
3. Extract the actual ZIP into a new writable folder with an empty `data/`.
   Launch it without `YOSHI_DATA_DIR` or launcher overrides, from another working
   directory. Verify the native ROM picker, Cancel, rejection of a wrong ROM,
   selection from a Unicode path, direct startup on the next launch, and
   Game > Select ROM ownership/cancellation. Check Explorer's egg icon, absence
   of a console, and `data/runtime.log`. Keep this test extraction private.
4. Prepare into a new output directory:

   ```powershell
   python tools/release.py prepare --tag windows-v1.0.1 --archive dist/yoshis-story-recomp-windows-x64.zip --output dist/releases/windows-v1.0.1
   ```

   This checks the clean source on `origin/main`, exact patched dependency
   trees, package allowlist, and executable identity. It creates:
   `Yoshis-Story-1.0.1-windows-x64.zip`, `.inventory.json`, and `.sha256`.
   It refuses to overwrite existing release artifacts.
5. Create a GitHub **draft** for `windows-v1.0.1`, targeting the exact commit in the
   inventory, and attach only those three files. Write the release notes to a
   UTF-8 file and pass it to `gh release create --draft --target <commit>
   --notes-file <file>`; use `--repo TK22-26/yoshis-story-recomp` explicitly.
6. Create an annotated `windows-v1.0.1` tag at that same commit and push it. The release
   workflow runs tests, downloads the draft, and verifies all three assets.
   Tag pushes verify the draft and **do not publish it**.

## Publish

Once publication is authorized, run the **Verify or publish Windows release**
workflow manually on the version tag with `publish` set to true. A branch
reference is rejected. The workflow requires the CMake version and tag to agree,
runs the tests, re-downloads the draft, checks hashes and the executable's clean
source identity, and publishes it as the latest stable release.

Never move a published tag or replace published files. Use a new version for
changed source. After publishing, verify the release status, download links,
asset names and digests. Keep the private ROM, test data and debug symbols local.
