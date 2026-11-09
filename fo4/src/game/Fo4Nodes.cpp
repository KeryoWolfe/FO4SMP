#include "Fo4Nodes.h"

#include "Offsets.h"

namespace hdt::fo4
{
	RE::NiNode* CreateNode(const char* a_name, const RE::NiTransform& a_local)
	{
		using Allocate_t = void*(void* a_heap, std::size_t a_size, std::size_t a_alignment, bool a_aligned);
		using Ctor_t = RE::NiNode*(void* a_node, std::uint16_t a_children);
		static REL::Relocation<std::uintptr_t> heap{ FO4SMP::Offsets::MainHeap };
		static REL::Relocation<Allocate_t*> allocate{ FO4SMP::Offsets::HeapAllocate };
		static REL::Relocation<Ctor_t*> ctor{ FO4SMP::Offsets::NiNodeCtor };

		void* memory = allocate(reinterpret_cast<void*>(heap.address()), 0x140, 0x10, true);  // sizeof(NiNode) in 1.11.240
		if (!memory) {
			return nullptr;
		}
		auto* node = ctor(memory, 0);
		node->name = a_name;
		node->local = a_local;
		node->world = a_local;
		return node;
	}
}
