#include "CodeScan.h"

namespace FO4SMP::CodeScan
{
	namespace
	{
		// x64 RUNTIME_FUNCTION: RVAs of the code's start and end and of its UNWIND_INFO.
		struct RuntimeFunction
		{
			std::uint32_t begin;
			std::uint32_t end;
			std::uint32_t unwind;
		};
		static_assert(sizeof(RuntimeFunction) == 12);

		std::uintptr_t Base() noexcept { return REX::FModule::GetExecutingModule().GetBaseAddress(); }

		template <class T>
		T At(std::uintptr_t a_address) noexcept
		{
			T value;
			std::memcpy(&value, reinterpret_cast<const void*>(a_address), sizeof(T));
			return value;
		}

		// The executable's exception directory (data directory 3), from its PE headers in memory.
		std::span<const RuntimeFunction> Table()
		{
			static const auto table = []() -> std::span<const RuntimeFunction> {
				const auto base = Base();
				if (At<std::uint16_t>(base) != 0x5A4D) {  // "MZ"
					return {};
				}
				const auto nt = base + At<std::uint32_t>(base + 0x3C);
				if (At<std::uint32_t>(nt) != 0x00004550 || At<std::uint16_t>(nt + 0x18) != 0x20B) {  // "PE\0\0", PE32+
					return {};
				}
				const auto directory = nt + 0x18 + 0x70 + 3 * 8;  // optional header, its data directories, entry 3
				const auto rva = At<std::uint32_t>(directory);
				const auto size = At<std::uint32_t>(directory + 4);
				if (rva == 0 || size < sizeof(RuntimeFunction)) {
					return {};
				}
				return { reinterpret_cast<const RuntimeFunction*>(base + rva), size / sizeof(RuntimeFunction) };
			}();
			return table;
		}

		// The function a table entry belongs to: follows UNW_FLAG_CHAININFO (4) to the first chunk's begin RVA.
		std::uint32_t RootOf(const RuntimeFunction& a_entry)
		{
			const auto base = Base();
			std::uint32_t begin = a_entry.begin;
			std::uint32_t unwind = a_entry.unwind;
			for (int depth = 0; depth < 32; ++depth) {
				if (unwind & 1) {  // points at another RUNTIME_FUNCTION (rare): take its function
					const auto& other = *reinterpret_cast<const RuntimeFunction*>(base + (unwind & ~1u));
					begin = other.begin;
					unwind = other.unwind;
					continue;
				}
				const auto info = base + unwind;
				const std::uint8_t flags = At<std::uint8_t>(info) >> 3;
				if (!(flags & 4)) {
					break;
				}
				const std::uint8_t codes = At<std::uint8_t>(info + 2);
				const auto chained = info + 4 + ((codes + 1u) & ~1u) * 2;  // the parent RUNTIME_FUNCTION follows the codes
				begin = At<std::uint32_t>(chained);
				unwind = At<std::uint32_t>(chained + 8);
			}
			return begin;
		}

		// The function's own entry (begin == a_rva), or null. The table is sorted by begin.
		const RuntimeFunction* EntryAt(std::uint32_t a_rva)
		{
			const auto table = Table();
			const auto it = std::lower_bound(table.begin(), table.end(), a_rva, [](const RuntimeFunction& a_entry, std::uint32_t a_value) { return a_entry.begin < a_value; });
			return it != table.end() && it->begin == a_rva ? &*it : nullptr;
		}
	}

	std::vector<Range> Chunks(std::uintptr_t a_function)
	{
		std::vector<Range> out;
		const auto base = Base();
		if (a_function <= base) {
			return out;
		}
		const auto rva = static_cast<std::uint32_t>(a_function - base);
		const auto* first = EntryAt(rva);
		if (!first || RootOf(*first) != rva) {
			return out;
		}
		// Chunks of one function sit near it in practice (the farthest seen: 0x3622 bytes, OG); look within 1 MB.
		const auto table = Table();
		const auto from = std::lower_bound(table.begin(), table.end(), rva > 0x100000 ? rva - 0x100000 : 0u, [](const RuntimeFunction& a_entry, std::uint32_t a_value) { return a_entry.begin < a_value; });
		for (auto it = from; it != table.end() && it->begin < rva + 0x100000; ++it) {
			if (RootOf(*it) == rva) {
				out.push_back({ base + it->begin, base + it->end });
			}
		}
		return out;
	}

	bool IsFunctionStart(std::uintptr_t a_address)
	{
		const auto base = Base();
		if (a_address <= base) {
			return false;
		}
		const auto rva = static_cast<std::uint32_t>(a_address - base);
		const auto* entry = EntryAt(rva);
		return entry && RootOf(*entry) == rva;
	}

	std::uintptr_t CallTarget(std::uintptr_t a_site)
	{
		if (At<std::uint8_t>(a_site) != 0xE8) {
			return 0;
		}
		return a_site + 5 + At<std::int32_t>(a_site + 1);
	}

	std::vector<std::uintptr_t> CallsTo(std::uintptr_t a_function, std::uintptr_t a_target)
	{
		std::vector<std::uintptr_t> out;
		for (const auto& chunk : Chunks(a_function)) {
			for (auto p = chunk.begin; p + 5 <= chunk.end; ++p) {
				if (CallTarget(p) == a_target) {
					out.push_back(p);
				}
			}
		}
		std::ranges::sort(out);
		return out;
	}
}
