#pragma once

#include "Patterns/hdtPatternLibrary.h"

namespace hdt
{
	struct GlobalConfig;

	// The name maps FSMP's DefaultBBP passes the system creator (FSMP's hdtDefaultBBP.h types, which
	// the Fallout 4 build doesn't compile: its scanning half is Skyrim's).
	struct DefaultBBP
	{
		using NameSet_t = std::unordered_set<std::string>;
		using NameMap_t = std::unordered_map<std::string, NameSet_t>;
		using PhysicsFile_t = std::pair<std::string, NameMap_t>;
	};
}

namespace hdt::fo4
{
	// The settings FSMP's game layer reads from SkyrimPhysicsWorld, for the Fallout 4 layer.
	struct Settings
	{
		bool clampRotations = true;
		float rotationSpeedLimit = 10.0f;
		bool unclampedResets = true;
		float unclampedResetAngle = 120.0f;
	};

	const Settings& GetSettings();
	void ApplySettings(const GlobalConfig& a_config);

	// <pattern> libraries shared by every physics XML: Data/F4SE/Plugins/FO4SMP/patterns/*.xml, read once.
	const std::vector<PatternLibrary>& GetPatternLibraries();
}
