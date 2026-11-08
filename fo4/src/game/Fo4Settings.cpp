#include "Fo4Settings.h"

#include "GlobalConfig.h"

namespace hdt::fo4
{
	namespace
	{
		Settings g_settings;
	}

	const Settings& GetSettings() { return g_settings; }

	void ApplySettings(const GlobalConfig& a_config)
	{
		g_settings.clampRotations = a_config.clampRotations;
		g_settings.rotationSpeedLimit = a_config.rotationSpeedLimit;
		g_settings.unclampedResets = a_config.unclampedResets;
		g_settings.unclampedResetAngle = a_config.unclampedResetAngle;
	}

	const std::vector<PatternLibrary>& GetPatternLibraries()
	{
		static const std::vector<PatternLibrary> libraries = scanPatternDir(std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "FO4SMP" / "patterns");
		return libraries;
	}
}
