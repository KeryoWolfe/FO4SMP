#!/usr/bin/env bash
# Builds and runs FSMP's GlobalConfig tests plus a check of FO4SMP's shipped configs.json.
# Off Windows: needs g++ (C++20), rapidjson and doctest headers. GlobalConfig is game-free, so this runs anywhere.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; root="$here/../.."
out="$(mktemp -d)"
g++ -std=c++20 -O1 -I "$root/src" -DFO4SMP_CONFIGS_JSON="\"$here/../package/F4SE/Plugins/FO4SMP/configs.json\"" \
	"$root/tests/config/main.cpp" "$root/tests/config/test_globalconfig.cpp" "$here/test_shipped_config.cpp" "$root/src/GlobalConfig.cpp" \
	-o "$out/config_tests"
"$out/config_tests"
