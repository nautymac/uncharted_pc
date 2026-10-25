#!/usr/bin/env bash
# bbport (Uncharted) stereo 3D add-on: dist/uncharted-3d-windows.zip, unpacked over the original
# PC port's uncharted-windows.zip. Only what the original does not have: bin/bb-probe.exe (the 3D
# build, stripped), bin/ps2sr.dll (the SR panel weaver bridge), the batch files (SR monitor / side by side) and the two READMEs (Korean, English)
# (packaging/windows/3d/) and the MSYS2 DLLs bb-probe.exe loads: the original package's DLLs
# are from its own MSYS2 build and the 3D executable fails to start on them (entry point not
# found, 0xc0000139). BB_3D_WITH_DLLS=0 leaves them out. Run from an MSYS2 CLANG64 shell.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
[[ -f out/bb-probe.exe ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
[[ -f out/ps2sr.dll ]] || echo 'out/ps2sr.dll missing: the package weaves nothing (side by side only)' >&2
stage=out/stage3d
dest=$stage/uncharted-windows
rm -rf "$stage"
mkdir -p "$dest/bin"
llvm-strip --strip-debug -o "$dest/bin/bb-probe.exe" out/bb-probe.exe
[[ -f out/ps2sr.dll ]] && cp out/ps2sr.dll "$dest/bin/"
# DLLs this build loads that the original package (0.3.9) does not ship.
for dll in libomp.dll; do cp /clang64/bin/$dll "$dest/bin/"; done
if [[ ${BB_3D_WITH_DLLS:-1} == 1 ]]; then
    ldd "$dest/bin/bb-probe.exe" | awk '/\/clang64\/bin\// {print $3}' | sort -u | while read -r dll; do
        cp -u "$dll" "$dest/bin/"
    done
fi
cp packaging/windows/3d/Uncharted-3D-SR.bat packaging/windows/3d/Uncharted-3D-SBS.bat \
   packaging/windows/3d/README-3D-KO.txt packaging/windows/3d/README-3D-EN.txt "$dest/"
# The batch files need CRLF for cmd.exe.
for bat in "$dest"/*.bat; do sed -i 's/\r$//; s/$/\r/' "$bat"; done
mkdir -p dist
rm -f dist/uncharted-3d-windows.zip
# Not Compress-Archive, which writes backslashes that Linux tools keep in the file names.
(cd "$stage" && python -c "
import pathlib, zipfile
with zipfile.ZipFile('../../dist/uncharted-3d-windows.zip', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(pathlib.Path('uncharted-windows').rglob('*')): z.write(p)")
du -sh "$dest" dist/uncharted-3d-windows.zip
unzip -l dist/uncharted-3d-windows.zip
