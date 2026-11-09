#pragma once

// Milestone 5: FSMP's DynamicHDT script functions for Fallout 4 (Scripts\Source\User\DynamicHDT.psc), with the two
// extras FO4 Faster HDT-SMP added (HasPhysicsSystem, IsSoftSuspended), and DynamicHDT's persisted physics file
// overrides in the F4SE co-save.

namespace FO4SMP::Papyrus
{
	// Registers the functions with F4SE's Papyrus interface and the co-save callbacks. Call at plugin load.
	bool Install();
}
