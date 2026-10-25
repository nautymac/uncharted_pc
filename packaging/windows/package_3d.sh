#!/usr/bin/env bash
# bbport (Uncharted) stereo 3D add-on: dist/uncharted-3d-windows.zip, unpacked over the original
# PC port's uncharted-windows.zip of the same version. Only what differs from the original:
# - Uncharted.exe and _internal/: the launcher of this branch (its 3D page: Off / SR monitor / SBS,
#   updates from the fork's releases), frozen with PyInstaller as packaging/windows/package.sh does;
# - bin/bb-probe.exe (the 3D build, stripped), bin/ps2sr.dll (the SR panel weaver bridge) and the
#   MSYS2 DLLs bb-probe.exe loads: the original package's DLLs are from its own MSYS2 build and the
#   3D executable fails to start on them (entry point not found, 0xc0000139). BB_3D_WITH_DLLS=0
#   leaves them out;
# - the two READMEs (Korean, English) from packaging/windows/3d/.
# Run from an MSYS2 CLANG64 shell after `bash build.sh`. Freezing uses a Windows Python 3.10+
# (python.org; WINPYTHON overrides) and the private venv out/pyenv, like package.sh.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
[[ -f out/bb-probe.exe ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
[[ -f out/ps2sr.dll ]] || echo 'out/ps2sr.dll missing: the package weaves nothing (side by side only)' >&2

# The launcher, frozen exactly as package.sh freezes it.
python=${WINPYTHON:-}
if [[ -z $python ]]; then
    for candidate in /c/Python3*/python.exe "${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}"/Programs/Python/Python3*/python.exe; do
        [[ -x $candidate ]] && python=$candidate
    done
fi
[[ -n $python ]] || { echo 'Need a Windows Python 3 (python.org) or WINPYTHON=path\to\python.exe' >&2; exit 1; }
export USERPROFILE=${USERPROFILE:-$(cygpath -w "/c/Users/$(id -un)")}
if [[ ! -x out/pyenv/Scripts/python.exe ]]; then
    "$python" -m venv out/pyenv
fi
out/pyenv/Scripts/python.exe -m pip install -q --disable-pip-version-check pyinstaller pillow tkinterdnd2
hidden=()
for module in argparse base64 collections hashlib json re shutil struct tempfile zlib ctypes.wintypes cheats \
              uncharted_art uncharted_fonts uncharted_i18n uncharted_widgets bbport_steam PIL.ImageTk; do
    hidden+=(--hidden-import "$module")
done
out/pyenv/Scripts/python.exe -m PyInstaller --noconfirm --clean --log-level WARN --windowed \
    --name Uncharted --icon "$(cygpath -w "$PWD/launcher/uncharted.ico")" --distpath out/pyi-dist \
    --workpath out/pyi-work --specpath out/pyi-work --paths "$(cygpath -w "$PWD/scripts")" \
    --paths "$(cygpath -w "$PWD/launcher")" \
    --add-data "$(cygpath -w "$PWD/launcher/fonts");fonts" \
    --add-data "$(cygpath -w "$PWD/launcher/covers");covers" \
    "${hidden[@]}" \
    "$(cygpath -w "$PWD/launcher/uncharted_launcher.py")"

stage=out/stage3d
dest=$stage/uncharted-windows
rm -rf "$stage"
mkdir -p "$dest/bin"
cp -r out/pyi-dist/Uncharted/. "$dest/"
llvm-strip --strip-debug -o "$dest/bin/bb-probe.exe" out/bb-probe.exe
[[ -f out/ps2sr.dll ]] && cp out/ps2sr.dll "$dest/bin/"
# DLLs this build loads that the original package (0.3.9) does not ship.
for dll in libomp.dll; do cp /clang64/bin/$dll "$dest/bin/"; done
if [[ ${BB_3D_WITH_DLLS:-1} == 1 ]]; then
    ldd "$dest/bin/bb-probe.exe" | awk '/\/clang64\/bin\// {print $3}' | sort -u | while read -r dll; do
        cp -u "$dll" "$dest/bin/"
    done
fi
cp packaging/windows/3d/README-3D-KO.txt packaging/windows/3d/README-3D-EN.txt "$dest/"
mkdir -p dist
rm -f dist/uncharted-3d-windows.zip
# Not Compress-Archive, which writes backslashes that Linux tools keep in the file names.
(cd "$stage" && python -c "
import pathlib, zipfile
with zipfile.ZipFile('../../dist/uncharted-3d-windows.zip', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(pathlib.Path('uncharted-windows').rglob('*')): z.write(p)")
du -sh "$dest" dist/uncharted-3d-windows.zip
unzip -l dist/uncharted-3d-windows.zip | tail -3
