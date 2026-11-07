#pragma once

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hdt
{
	// The contents of defaultBBPs.xml, game-free: which physics file a shape name maps to, and the name
	// remaps applied before that lookup. Parsed here so both games share it; DefaultBBP (Skyrim) and the
	// Fallout 4 plugin apply it to their own meshes.
	struct DefaultBBPList
	{
		using RemapEntry_t = std::pair<int, std::string>;

		struct Remap
		{
			std::string name;
			std::set<RemapEntry_t> entries;
			std::unordered_set<std::string> required;
		};

		std::unordered_map<std::string, std::string> files;  // shape name -> physics XML
		std::vector<Remap> remaps;
		bool valid = false;  // false: empty input, or the root element isn't <default-bbps>
	};

	// Parses defaultBBPs.xml. Never throws: an invalid <map> or an unknown element is logged and skipped.
	DefaultBBPList parseDefaultBBPs(std::string_view xml);
}
