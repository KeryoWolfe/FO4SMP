#pragma once

// Finds game code by what it is, at load, in the running executable's memory: a function's code (its chunks, from the
// executable's unwind table, the same table the system uses to unwind the stack) and the calls in it to another
// function. Used where the place to hook differs between runtimes (OG, NG, AE), so no per-runtime offset is needed.
namespace FO4SMP::CodeScan
{
	struct Range
	{
		std::uintptr_t begin;
		std::uintptr_t end;
	};

	// The unwind table's entries for a_function: its first chunk and every chunk chained to it. Empty when a_function
	// isn't the start of a function in the table.
	std::vector<Range> Chunks(std::uintptr_t a_function);

	// True when a_address starts a function in the unwind table (not a chained chunk). Leaf functions that use no stack
	// have no entry, so false doesn't by itself mean wrong.
	bool IsFunctionStart(std::uintptr_t a_address);

	// Every `call rel32` (E8) in a_function's chunks whose target is a_target, in address order.
	std::vector<std::uintptr_t> CallsTo(std::uintptr_t a_function, std::uintptr_t a_target);

	// The target of the `call rel32` at a_site, or 0 if a_site doesn't start with E8.
	std::uintptr_t CallTarget(std::uintptr_t a_site);
}
