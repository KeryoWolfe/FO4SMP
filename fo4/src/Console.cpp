#include "Console.h"

#include "Offsets.h"

namespace FO4SMP::Console
{
	bool Print(std::string_view a_line)
	{
		static REL::Relocation<RE::ConsoleLog**> singleton{ Offsets::ConsoleLog_Singleton };
		auto* log = *singleton;
		if (!log) {
			return false;
		}
		const std::string line{ a_line };
		log->PrintLine("%s", line.c_str());
		return true;
	}
}
