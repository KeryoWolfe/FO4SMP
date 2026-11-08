#pragma once

// Precompiled header for FO4SMP. Besides CommonLibF4, it supplies what FSMP's physics core
// (../../src/hdtSkinnedMesh, XmlReader, GlobalConfig) gets from FSMP's own PCH and <Windows.h>
// without including them itself. fo4/tools/core-check finds that list; keep the two in step.

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>
#include <REL/Relocation.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <tbb/tbb.h>

using namespace std::literals;

namespace logger = F4SE::log;

// <Windows.h> types and macros the core uses (CommonLibF4 doesn't include <Windows.h>).
using UINT = unsigned int;
using BYTE = unsigned char;
#ifndef ZeroMemory
#	define ZeroMemory(a_dst, a_len) std::memset((a_dst), 0, (a_len))
#endif

namespace RE
{
	// From FSMP's PCH: compare a smart pointer with a raw one (the core finds systems in a
	// vector<BSTSmartPointer> by raw pointer). CommonLibF4 only compares two smart pointers.
	template <class T1, class T2, template <class> class R>
	[[nodiscard]] constexpr bool operator==(const BSTSmartPointer<T1, R>& a_lhs, T2* a_rhs)
	{
		return a_lhs.get() == a_rhs;
	}

	template <class T1, class T2, template <class> class R>
	[[nodiscard]] constexpr bool operator!=(const BSTSmartPointer<T1, R>& a_lhs, T2* a_rhs)
	{
		return !(a_lhs.get() == a_rhs);
	}
}

namespace hdt
{
	// FSMP's wrapper (from its PCH): adopt a raw pointer into a BSTSmartPointer.
	template <class T>
	[[nodiscard]] RE::BSTSmartPointer<T> make_smart(T* a_ptr)
	{
		RE::BSTSmartPointer<T> result;
		result.reset(a_ptr);
		return result;
	}
}

namespace std
{
	// The core keys unordered sets by BSFixedString. Pooled strings are interned, so the data pointer
	// identifies the string (as FSMP's PCH does for CommonLibSSE; CommonLibF4 has no hash for it).
	template <class CharT, bool CS>
	struct hash<RE::detail::BSFixedString<CharT, CS>>
	{
		[[nodiscard]] std::size_t operator()(const RE::detail::BSFixedString<CharT, CS>& a_key) const noexcept
		{
			return std::hash<const CharT*>{}(a_key.data());
		}
	};
}

#include "Version.h"
