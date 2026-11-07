#pragma once

namespace FO4SMP::AddressCheck
{
	// Checks each address in Offsets.h against what the plugin expects to find
	// there, logging one line per address. Returns true when all of them pass.
	// Reads memory only; patches nothing.
	bool Run();
}
