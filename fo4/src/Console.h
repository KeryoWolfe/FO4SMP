#pragma once

namespace FO4SMP::Console
{
	// Prints one line to the in-game console. Returns false when the console doesn't exist yet.
	//
	// CommonLibF4's ConsoleLog::GetSingleton uses next-gen ID 2690148, which isn't in Address Library
	// for 1.11.240; this uses 4797437, found in 1.11.240 as the pointer 976 console calls load
	// before calling ConsoleLog::Print / AddString (fo4/ADDRESSES.md).
	bool Print(std::string_view a_line);
}
