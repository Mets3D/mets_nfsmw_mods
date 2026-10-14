#!/usr/bin/env bash
# Builds NFSMW_XenonEffects on Linux from Tools/OtherMods/nfsmw-xenon-effects-torufox
# (https://github.com/TorutheRedFox/NFSMW_XenonEffects, a C++ rewrite of https://github.com/xan1242/NFSMW_XenonEffects).
# Three functions in it are MSVC inline assembly, which GCC cannot compile, so clang compiles it (target i686 MinGW, system MinGW
# headers and libraries, -fasm-blocks) and MinGW's g++ links it. The checkout is not modified: copies of the sources are made
# without what clang's assembler does not take (a BOM, non-ASCII characters in assembly comments, "ds:" segment prefixes).
# The repo does not contain what its Visual Studio project gets from NuGet and a submodule, so they come from:
#   EASTL 3.18.00, EABase, EAAssert  cloned by this script into Tools/OtherMods/ (EASTL 3.18 is the last with `allocator_eastl.cpp`, close to
#                                    the version the project lists)
#   injector, mINI                   from the checkout of the original, Tools/OtherMods/nfsmw-xenon-effects-xan1242 (git clone it and
#                                    `git submodule update --init`)
#
# usage: Tools/build_xenon_effects.sh [output dir]      (default: Tools/OtherMods/build; the result is NFSMW_XenonEffects.asi)
# needs: clang, i686-w64-mingw32-g++ / strip, the MinGW d3d9 / d3dx9 headers (Fedora: mingw32-gcc-c++, clang)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mods="$here/OtherMods"
src="$mods/nfsmw-xenon-effects-torufox"
base="$mods/nfsmw-xenon-effects-xan1242"
out="${1:-$mods/build}"
sysroot=/usr/i686-w64-mingw32/sys-root/mingw
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

[ -d "$src/XenonEffects/src" ] || { echo "no checkout at $src (git clone https://github.com/TorutheRedFox/NFSMW_XenonEffects.git there)" >&2; exit 1; }
[ -f "$base/includes/mINI/src/mini/ini.h" ] || { echo "no injector / mINI at $base (git clone https://github.com/xan1242/NFSMW_XenonEffects.git there, then git -C there submodule update --init)" >&2; exit 1; }
[ -d "$mods/eabase" ]  || git clone --depth 1 https://github.com/electronicarts/EABase.git "$mods/eabase"
[ -d "$mods/eaassert" ] || git clone --depth 1 https://github.com/electronicarts/EAAssert.git "$mods/eaassert"
[ -d "$mods/eastl" ]   || git clone --depth 1 --branch 3.18.00 https://github.com/electronicarts/EASTL.git "$mods/eastl"
mkdir -p "$out" "$work/inc" "$work/o"

# what the project finds through Visual Studio
echo '#pragma once' > "$work/inc/SDKDDKVer.h"
echo '#include <windows.h>' > "$work/inc/Windows.h"
ln -s "$base/includes/injector" "$work/inc/injector"
ln -s "$base/includes/mINI" "$work/inc/mINI"

flags=(--target=i686-w64-mingw32 --sysroot="$sysroot" -I"$work/inc" -I"$mods/eabase/include/Common" -I"$mods/eaassert/include"
    -I"$mods/eastl/include" -I"$src" -I"$src/XenonEffects/src" -I"$src/XenonEffects/src/Attrib" -I"$src/XenonEffects/src/Render"
    -I"$src/XenonEffects/src/UMath" -I"$src/Speed/src" -std=c++20 -fms-extensions -fasm-blocks -O2 -w -include cstring
    -DWIN32 -DNDEBUG -D_WINDOWS -D_USRDLL -D_CRT_SECURE_NO_WARNINGS
    '-D_NODISCARD=' '-D_Ret_notnull_=' '-D_Post_writable_byte_size_(x)=' '-D_VCRT_ALLOCATOR=')   # MSVC annotations

objects=()
for f in "$src"/XenonEffects/src/dllmain.cpp "$src"/XenonEffects/src/xSpark.cpp "$src"/XenonEffects/src/*/*.cpp; do
    copy="$work/$(basename "$f")"
    LC_ALL=C sed -E '1s/^\xEF\xBB\xBF//; s/[\x80-\xff]/_/g; s/(^|[ \t,\[])ds ?: ?/\1/g' "$f" > "$copy"
    # NGParticle has a 16-byte aligned member, so clang copies it with movaps; the array comes from calloc (8-byte aligned here):
    # an unaligned movaps is a general protection fault (reported as an access violation at 0xffffffff).
    sed -i 's/(NGParticle\*)calloc(MAX_NGPARTICLES, sizeof(NGParticle))/(NGParticle*)memset(_aligned_malloc(MAX_NGPARTICLES * sizeof(NGParticle), 16), 0, MAX_NGPARTICLES * sizeof(NGParticle))/' "$copy"
    clang++ "${flags[@]}" -I"$(dirname "$f")" -c "$copy" -o "$work/o/$(basename "$f" .cpp).o"
    objects+=("$work/o/$(basename "$f" .cpp).o")
done
for f in "$mods"/eastl/source/{allocator_eastl,assert,fixed_pool,hashtable,red_black_tree,string}.cpp "$mods"/eaassert/source/eaassert.cpp; do
    clang++ "${flags[@]}" -c "$f" -o "$work/o/e_$(basename "$f" .cpp).o"
    objects+=("$work/o/e_$(basename "$f" .cpp).o")
done

i686-w64-mingw32-g++ -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at -o "$out/NFSMW_XenonEffects.asi" "${objects[@]}" \
    -ld3d9 -ld3dx9_43 2>&1 | grep -v "drectve" || true
cp "$out/NFSMW_XenonEffects.asi" "$out/NFSMW_XenonEffects.asi.debug"   # with symbols, to name a crash address (objdump -d / nm)
i686-w64-mingw32-strip -s "$out/NFSMW_XenonEffects.asi"
echo "built $out/NFSMW_XenonEffects.asi"
