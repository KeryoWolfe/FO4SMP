#!/usr/bin/env bash
# Syntax-checks FSMP's physics core with stand-ins for what FO4SMP's PCH supplies (fo4pch_shim.h,
# make_smart_shim.h, RE_shim.h), to catch a name the PCH is missing. Errors from MSVC-only SIMD
# code (__m128 members and operators, which clang can't compile) are filtered out; anything left is real.
# usage: check-core-names.sh <bullet3 source dir with FSMP's patches applied>
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; src="$here/../../../src"; bullet="${1:?path to bullet3}"
# Only errors that name the SIMD types themselves count as noise: a broader filter ("invalid operands",
# "no matching function") once hid a real missing operator== that MSVC then reported.
noise='__m128|m128|vector of|must have at least one parameter of class|misplaced attributes'
left=0
for f in "$src"/hdtSkinnedMesh/*.cpp "$src"/XmlReader.cpp "$src"/hdtDefaultBBPList.cpp; do
	out=$(clang++ -std=c++23 -fsyntax-only -ferror-limit=0 -msse4.1 -mavx2 -mfma -Wno-everything -DBT_USE_SSE -DBT_USE_SSE_IN_API \
		-include "$here/fo4pch_shim.h" -include "$here/RE_shim.h" -include "$here/make_smart_shim.h" \
		-I"$here" -I"$bullet/src" -I"$src" -I"$src/hdtSkinnedMesh" "$f" 2>&1 | grep 'error:' | grep -vE "$noise")
	if [ -n "$out" ]; then echo "$out"; left=1; fi
done
[ $left -eq 0 ] && echo "no missing names (MSVC-only SIMD errors ignored)"
exit $left
