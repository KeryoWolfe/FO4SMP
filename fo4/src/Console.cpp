#include "Console.h"

#include "Offsets.h"

namespace FO4SMP::Console
{
	namespace
	{
		// In 1.11.240, ID 2248591 is the game's variadic, printf-style print: (log, format, ...).
		// CommonLibF4 declares it as Print(format, va_list) and its PrintLine passes a va_list,
		// so "%s" printed the va_list pointer's bytes (seen in game as "5" and four boxes).
		// Called directly here, as the game's own callers do ("Exported Reference: %s(%08x)", ...).
		using PrintFormatted_t = void(RE::ConsoleLog*, const char*, ...);
	}

	bool Print(std::string_view a_line)
	{
		static REL::Relocation<RE::ConsoleLog**> singleton{ Offsets::ConsoleLog_Singleton };
		static auto* printFormatted = reinterpret_cast<PrintFormatted_t*>(REL::ID(2248591).address());

		auto* log = *singleton;
		if (!log) {
			return false;
		}
		const std::string line{ a_line };
		printFormatted(log, "%s", line.c_str());
		return true;
	}
}
