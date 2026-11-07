#include "hdtDefaultBBP.h"

#include "NetImmerseUtils.h"
#include "XmlReader.h"
#include "hdtDefaultBBPList.h"

namespace hdt
{
	DefaultBBP* DefaultBBP::instance()
	{
		static DefaultBBP s;
		return &s;
	}

	DefaultBBP::PhysicsFile_t DefaultBBP::scanBBP(RE::NiNode* scan)
	{
		for (int i = 0; i < scan->extraDataSize; ++i) {
			auto stringData = netimmerse_cast<RE::NiStringExtraData*>(scan->extra[i]);
			if (stringData && stringData->name == "HDT Skinned Mesh Physics Object" && stringData->value) {
				return { { std::string(stringData->value) }, defaultNameMap(scan) };
			}
		}

		return scanDefaultBBP(scan);
	}

	DefaultBBP::DefaultBBP()
	{
		loadDefaultBBPs();
	}

	void DefaultBBP::loadDefaultBBPs()
	{
		auto path = "SKSE/Plugins/hdtSkinnedMeshConfigs/defaultBBPs.xml";

		auto list = parseDefaultBBPs(readAllFile(path));
		bbpFileList = std::move(list.files);
		remaps = std::move(list.remaps);
	}

	DefaultBBP::PhysicsFile_t DefaultBBP::scanDefaultBBP(RE::NiNode* armor)
	{
		static std::mutex s_lock;
		std::lock_guard<std::mutex> l(s_lock);

		if (bbpFileList.empty()) {
			return { { std::string("") }, {} };
		}

		auto remappedNames = DefaultBBP::instance()->getNameMap(armor);

		auto it = std::find_if(bbpFileList.begin(), bbpFileList.end(), [&](const std::pair<std::string, std::string>& e) { return remappedNames.find(e.first) != remappedNames.end(); });
		return { it == bbpFileList.end() ? "" : it->second, remappedNames };
	}

	DefaultBBP::NameMap_t DefaultBBP::getNameMap(RE::NiNode* armor)
	{
		auto nameMap = defaultNameMap(armor);

		for (auto remap : remaps) {
			bool doRemap = true;
			for (auto req : remap.required) {
				if (nameMap.find(req) == nameMap.end()) {
					doRemap = false;
				}
			}

			if (doRemap) {
				auto start = std::find_if(remap.entries.rbegin(), remap.entries.rend(), [&](const auto& e) { return nameMap.find(e.second) != nameMap.end(); });
				auto end = std::find_if(start, remap.entries.rend(), [&](const auto& e) { return e.first != start->first; });
				if (start != remap.entries.rend()) {
					auto&& s = nameMap.insert({ remap.name, {} }).first;
					std::for_each(start, end, [&](const auto& e) {
						auto it = nameMap.find(e.second);
						if (it != nameMap.end()) {
							std::for_each(it->second.begin(), it->second.end(), [&](const std::string& name) {
								s->second.insert(name);
							});
						}
					});
				}
			}
		}
		return nameMap;
	}

	DefaultBBP::NameMap_t DefaultBBP::defaultNameMap(RE::NiNode* armor)
	{
		std::unordered_map<std::string, std::unordered_set<std::string>> nameMap;

		// This case never happens to a lurker skeleton, thus we don't need to test.
		auto skinned = findNode(armor, "BSFaceGenNiNodeSkinned");
		if (skinned) {
			const auto& skinnedNodechildren = skinned->GetChildren();
			for (uint16_t i = 0; i < skinnedNodechildren.size(); ++i) {
				if (!skinnedNodechildren[i]) {
					continue;
				}

				auto tri = skinnedNodechildren[i]->AsTriShape();
				if (!tri || !tri->name.size()) {
					continue;
				}

				nameMap.emplace(tri->name.c_str(), std::unordered_set<std::string>{ std::string(tri->name.c_str()) });
			}
		}

		const auto& armorNodechildren = armor->GetChildren();
		for (uint16_t i = 0; i < armorNodechildren.size(); ++i) {
			if (!armorNodechildren[i]) {
				continue;
			}

			auto tri = armorNodechildren[i]->AsTriShape();
			if (!tri || !tri->name.size()) {
				continue;
			}

			nameMap.emplace(tri->name.c_str(), std::unordered_set<std::string>{ std::string(tri->name.c_str()) });
		}

		return nameMap;
	}
}
