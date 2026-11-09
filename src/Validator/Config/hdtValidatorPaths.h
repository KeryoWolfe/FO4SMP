#pragma once

namespace hdt
{
	// File paths used by the physics asset validators, relative to the game working directory.
#ifdef FO4SMP_BUILD
	// Fallout 4 (FO4SMP): shipped beside the plugin's configs.
	inline constexpr const char* kPhysicsXSDPath =
		"data/F4SE/Plugins/FO4SMP/hdtSMP64.xsd";
#else
	inline constexpr const char* kPhysicsXSDPath =
		"data/skse/plugins/hdtSkinnedMeshConfigs/hdtSMP64.xsd";
#endif
	inline constexpr const char* kPhysicsSCHPath =
		"data/skse/plugins/hdtSkinnedMeshConfigs/hdtSMP64.sch";
}  // namespace hdt
