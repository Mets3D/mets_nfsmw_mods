#!/usr/bin/env bash
# Fetches everything the .gitignore'd parts of Tools/ expect (reference clones, Python libs). Safe to re-run: skips what exists.
# usage: Tools/setup_references.sh        (from anywhere; needs git and Python 3 with pip)
set -euo pipefail
cd "$(dirname "$0")/.."

git submodule update --init --recursive   # Tools/nfsmw-2005-sdk, the headers the mods build against

clone() {  # clone <url> <dir> [extra git args]
  local url=$1 dir=$2; shift 2
  [ -d "$dir/.git" ] && { echo "have $dir"; return; }
  git clone --depth 1 "$@" "$url" "$dir"
}
clone https://github.com/dbalatoni13/nfsmw.git                Tools/nfsmw-decomp
clone https://github.com/s-b-repo/nfsmw-2005-re.git          Tools/nfsmw-2005-re
clone https://github.com/ExOptsTeam/NFSMWExOpts.git           Tools/OtherMods/nfsmw-extra-options
clone https://github.com/ThirteenAG/WidescreenFixesPack.git   Tools/OtherMods/widescreenfixespack
clone https://github.com/TorutheRedFox/NFSMW_XenonEffects.git Tools/OtherMods/nfsmw-xenon-effects-torufox
clone https://github.com/xan1242/NFSMW_XenonEffects.git       Tools/OtherMods/nfsmw-xenon-effects-xan1242 --recurse-submodules

py=python; command -v python >/dev/null || py=python3
[ -d Tools/re/pylib/capstone ] || "$py" -m pip install -q capstone --target Tools/re/pylib
[ -d Tools/vlt-tools/pylib/yaml ] || "$py" -m pip install -q pyyaml --target Tools/vlt-tools/pylib

# Not fetched: Attribulator (see Mods/overhaul_for_redux_v3/ReadMe.md) and the game installs in "Game Versions/" (bring your own copies of NFS: MW 1.3).
echo done
