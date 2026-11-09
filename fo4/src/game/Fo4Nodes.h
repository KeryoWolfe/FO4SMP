#pragma once

// Milestone 5: NiNodes made by FO4SMP, through the game's own allocator and constructor (see Offsets.h), for
// bones Fallout 4 shares between actors (face generation's head-part bones) that each actor needs its own of.

namespace hdt::fo4
{
	// A new, parentless NiNode named a_name with local transform a_local (world = local). Its reference count is
	// zero: hold it in an RE::NiPointer. nullptr if the allocation failed.
	RE::NiNode* CreateNode(const char* a_name, const RE::NiTransform& a_local);
}
