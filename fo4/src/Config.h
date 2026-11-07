#pragma once

#include "GlobalConfig.h"
#include "hdtDefaultBBPList.h"

namespace FO4SMP::Config
{
	// Data\F4SE\Plugins\FO4SMP, relative to the game folder (MO2's virtual folder included).
	[[nodiscard]] std::filesystem::path Dir();

	// configs.json (the shipped defaults), then userConfigs.json on top, as FSMP's loadConfig.
	// Missing or broken files leave the defaults; each step is logged.
	[[nodiscard]] hdt::GlobalConfig Load();

	// Writes every loaded value to the log.
	void Log(const hdt::GlobalConfig& a_config);

	// defaultBBPs.xml: which physics XML a mesh without its own gets, by shape name. Logs what it read.
	[[nodiscard]] hdt::DefaultBBPList LoadDefaultBBPs();
}
