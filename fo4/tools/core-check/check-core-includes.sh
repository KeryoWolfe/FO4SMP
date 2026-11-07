#!/usr/bin/env bash
# Checks that FSMP's physics core (src/hdtSkinnedMesh) needs no game headers:
# every file must preprocess with only Bullet, oneTBB and RE_shim.h (stand-ins
# for BSFixedString, BSTSmartPointer, BSIntrusiveRefCounted).
#
# A full compile isn't possible off MSVC: the core uses MSVC's __m128 members
# and operators. This checks includes only.
#
# usage: check-core-includes.sh <bullet3 source dir with FSMP's patches applied>
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
core="$here/../../../src/hdtSkinnedMesh"
bullet="${1:?path to bullet3}"
n=0; failed=0
for f in "$core"/*.cpp; do
	if clang++ -std=c++23 -E -msse4.1 -include "$here/RE_shim.h" -I"$here" -I"$bullet/src" -I"$core" "$f" -o /dev/null; then
		n=$((n+1))
	else
		echo "FAIL: $(basename "$f")"; failed=1
	fi
done
echo "$n core files preprocess without game headers"
exit $failed
