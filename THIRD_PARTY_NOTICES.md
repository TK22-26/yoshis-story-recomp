# Third-party notices

This fork builds on damobeanrod's Yoshi's Story recompilation, N64Recomp,
N64ModernRuntime, RT64, SDL2, and their dependencies. Their notices are in
`licenses/`. Those files retain the original authors' terms; they do not
relicense Nintendo's game or assets.

The Windows binary links N64ModernRuntime (GPLv3) and includes the pinned
RT64 and N64Recomp dependencies. Corresponding project source, build scripts,
dependency revisions, and runtime patches are available at
<https://github.com/TK22-26/yoshis-story-recomp>. Consult the README for the
build procedure. A user-supplied ROM is required locally.

The DXC notice comes from Microsoft's DirectXShaderCompiler revision
`0dc8d9060df41901dbfa5ad7190da03e33d09518`, matching the bundled x64 compiler.
SDL2's notice comes from the pinned 2.32.10 development archive. Other notices
come from the dependency revisions referenced by this project, except the
nlohmann/json MIT notice (v3.11.3), whose copyright and terms cover that library.

The Windows package includes the unmodified x64 Microsoft.VC143.CRT libraries
from Visual Studio 2022's redistributable directory. These retain Microsoft's
terms; see `licenses/Microsoft-Visual-Cpp.txt`. Their exact hashes are included
in the release inventory.

The compact menu layout follows TK22-26's Jet Force Gemini project. Fonts are
loaded from the operating system at runtime. The egg icon is original project
art produced by `tools/gen_icon.py`. No game artwork, audio, textures, ROMs,
personal saves, or bundled font files are included in the Windows package.

The controller diagrams were supplied by TK22-26 for this interface. The About
links use GitHub's unmodified white Invertocat from its official logo download,
under the permitted use of linking to GitHub projects. GitHub's mark remains its
trademark and does not indicate endorsement. Source and provenance are recorded
in `assets/README.md`; see <https://brand.github.com/foundations/logo> and
<https://docs.github.com/en/site-policy/other-site-policies/github-logo-policy>.

Embedded SVGs use NanoSVG revision `239e102ec2c691f2902e20ace2ed36ee4a35cfe6`,
under the zlib license retained in `licenses/NanoSVG.txt`.
