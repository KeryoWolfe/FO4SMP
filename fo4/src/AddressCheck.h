#pragma once

namespace FO4SMP::AddressCheck
{
	struct Result
	{
		int ok = 0;
		int hooked = 0;  // the address resolves, but another plugin has patched the code there (can't compare)
		int failed = 0;  // missing from Address Library, or the code there isn't what 1.11.240 has

		[[nodiscard]] bool Passed() const noexcept { return failed == 0; }
	};

	// Checks each address in Offsets.h against what the plugin expects to find there, logging one
	// line per address. Reads memory only; patches nothing.
	Result Run();
}
