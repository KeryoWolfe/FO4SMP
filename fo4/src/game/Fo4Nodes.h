#pragma once

// Milestone 5: NiNodes made by FO4SMP, through the game's own allocator and constructor (see Offsets.h), for
// bones Fallout 4 shares between actors (face generation's head-part bones) that each actor needs its own of.

namespace hdt::fo4
{
	// A new, parentless NiNode named a_name with local transform a_local (world = local). Its reference count is
	// zero: hold it in an RE::NiPointer. nullptr if the allocation failed.
	RE::NiNode* CreateNode(const char* a_name, const RE::NiTransform& a_local);

	// The game's main heap (F4SE's Heap_Allocate / Heap_Free, IDs in Offsets.h). CommonLibF4's RE::malloc and RE::free
	// can't be used on 1.11.240: MemoryManager::GetSingleton's ID (2193197) isn't in Address Library 1.11.240, and
	// CommonLibF4 then silently takes the next ID's address (run 43: a crash at the VM's start).
	void* GameAllocate(std::size_t a_size);
	void GameFree(void* a_memory);
}
