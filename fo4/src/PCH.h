#pragma once

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>
#include <REL/Relocation.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string_view>

using namespace std::literals;

namespace logger = F4SE::log;

#include "Version.h"
