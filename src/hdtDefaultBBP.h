#pragma once

#include "hdtDefaultBBPList.h"

namespace hdt
{
	class DefaultBBP
	{
	public:
		using RemapEntry_t = DefaultBBPList::RemapEntry_t;
		using NameSet_t = std::unordered_set<std::string>;
		using NameMap_t = std::unordered_map<std::string, NameSet_t>;
		using PhysicsFile_t = std::pair<std::string, NameMap_t>;
		using Remap = DefaultBBPList::Remap;

		static DefaultBBP* instance();
		PhysicsFile_t scanBBP(RE::NiNode* scan);

	private:
		DefaultBBP();

		std::unordered_map<std::string, std::string> bbpFileList;
		std::vector<Remap> remaps;

		void loadDefaultBBPs();
		PhysicsFile_t scanDefaultBBP(RE::NiNode* scan);
		NameMap_t getNameMap(RE::NiNode* armor);
		NameMap_t defaultNameMap(RE::NiNode* armor);
	};
}
