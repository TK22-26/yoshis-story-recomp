param(
    [string]$Rom,
    [string]$BuildDirectory = 'build',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

Push-Location $repo
try {
    # Load the x64 SDK, linker and C++ standard library even from ordinary PowerShell.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 Desktop development with C++ first.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Visual Studio C++ build tools were not found.' }
    & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    $toolDirs = @(
        "$repo\.deps\llvm\bin",
        "$env:ProgramFiles\LLVM\bin",
        "$vs\VC\Tools\Llvm\x64\bin",
        "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
        "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
    )
    $env:PATH = ($toolDirs -join ';') + ';' + $env:PATH
    foreach ($tool in @('git', 'cmake', 'ninja', 'clang-cl', 'clang', 'python')) {
        if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool. See the Windows prerequisites in README.md." }
    }

    if ($Rom) {
        $sourceRom = (Resolve-Path -LiteralPath $Rom).Path
        $destinationRom = Join-Path $repo 'data\yoshis_story.z64'
        if ($sourceRom -ne $destinationRom) { Copy-Item -LiteralPath $sourceRom -Destination $destinationRom }
    }
    if (!(Test-Path -LiteralPath 'data/yoshis_story.z64')) { throw 'Supply your US ROM with -Rom or put it at data/yoshis_story.z64.' }

    Invoke-Checked git @('submodule', 'update', '--init', '--recursive')
    $runtime = Join-Path $repo '.deps\N64ModernRuntime'
    $runtimeCommit = 'cdf5abbd5026fef5c364c676e4667c45e42b6863'
    if (!(Test-Path -LiteralPath "$runtime/.git")) {
        Invoke-Checked git @('clone', 'https://github.com/N64Recomp/N64ModernRuntime', $runtime)
        Invoke-Checked git @('-C', $runtime, 'checkout', $runtimeCommit)
    }
    $actualCommit = & git -C $runtime rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $runtimeCommit) {
        throw "The runtime must be at $runtimeCommit. Your existing checkout was left intact."
    }
    Invoke-Checked git @('-C', $runtime, 'submodule', 'update', '--init', '--recursive')

    $sdlVersion = '2.32.10'
    $sdl = Join-Path $repo ".deps\SDL2-$sdlVersion"
    if (!(Test-Path -LiteralPath "$sdl/cmake/sdl2-config.cmake")) {
        $downloads = New-Item -ItemType Directory -Force '.deps/downloads'
        $archive = Join-Path $downloads.FullName "SDL2-devel-$sdlVersion-VC.zip"
        Invoke-WebRequest -UseBasicParsing "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVersion/SDL2-devel-$sdlVersion-VC.zip" -OutFile $archive
        $expectedHash = 'AF347939395A58B365846AAEA27391E69F9EC9D4DD650D6AC40802159B418A6E'
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'SDL2 archive checksum mismatch.'
        }
        Expand-Archive -LiteralPath $archive -DestinationPath '.deps' -Force
    }

    Invoke-Checked cmake @('-S', '.', '-B', $BuildDirectory, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl',
        "-DN64MR_PATH=$runtime", "-DSDL2_DIR=$sdl/cmake")
    Invoke-Checked cmake @('--build', $BuildDirectory, '--parallel', '8')
    Invoke-Checked ctest @('--test-dir', $BuildDirectory, '--output-on-failure')
    if ($Package) {
        Invoke-Checked cpack @('--config', "$BuildDirectory/CPackConfig.cmake", '-B', 'dist', '-C', 'Release')
        Invoke-Checked python @('tools/check_package.py', 'dist/yoshis-story-recomp-windows-x64.zip')
    }
    Write-Host "Built $BuildDirectory/yoshis_story_recomp.exe"
} finally {
    Pop-Location
}
