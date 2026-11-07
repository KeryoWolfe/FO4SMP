#include "Config.h"

namespace FO4SMP::Config
{
	namespace
	{
		std::optional<std::string> ReadFile(const std::filesystem::path& a_path)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				return std::nullopt;
			}
			std::ostringstream buffer;
			buffer << file.rdbuf();
			return buffer.str();
		}
	}

	std::filesystem::path Dir()
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "FO4SMP";
	}

	hdt::GlobalConfig Load()
	{
		hdt::GlobalConfig config{};

		const auto shipped = Dir() / "configs.json";
		if (const auto bytes = ReadFile(shipped)) {
			config = hdt::parseConfigJson(*bytes, config);
			logger::info("Config: read {} ({} bytes)", shipped.generic_string(), bytes->size());
		} else {
			logger::warn("Config: {} not found; using built-in defaults", shipped.generic_string());
		}

		const auto user = Dir() / "userConfigs.json";
		if (const auto bytes = ReadFile(user)) {
			config = hdt::parseConfigJson(*bytes, config);
			logger::info("Config: read {} on top ({} bytes)", user.generic_string(), bytes->size());
		} else {
			logger::info("Config: no {} (fine: it only exists once settings are changed)", user.generic_string());
		}

		return config;
	}

	void Log(const hdt::GlobalConfig& c)
	{
		logger::info("Config: smp logLevel={} clampRotations={} rotationSpeedLimit={} unclampedResets={} unclampedResetAngle={} useRealTime={}",
			c.logLevel, c.clampRotations, c.rotationSpeedLimit, c.unclampedResets, c.unclampedResetAngle, c.useRealTime);
		logger::info("Config: smp minCullingDistance={} autoAdjustMaxSkeletons={} maximumActiveSkeletons={} budgetMs={} sampleSize={}",
			c.minCullingDistance, c.autoAdjustMaxSkeletons, c.maximumActiveSkeletons, c.budgetMs, c.sampleSize);
		logger::info("Config: smp disable1stPersonViewPhysics={} skipDeadActors={} disableSMPHairWhenWigEquipped={} backupNodeByName={}",
			c.disable1stPersonViewPhysics, c.skipDeadActors, c.disableSMPHairWhenWigEquipped, c.backupNodeByName.size());
		logger::info("Config: solver numIterations={} erp={} min-fps={} maxSubSteps={}",
			c.numIterations, c.erp, c.minFps, c.maxSubSteps);
		logger::info("Config: wind enabled={} windStrength={} distanceForNoWind={} distanceForMaxWind={}",
			c.windEnabled, c.windStrength, c.distanceForNoWind, c.distanceForMaxWind);
	}

	hdt::DefaultBBPList LoadDefaultBBPs()
	{
		const auto path = Dir() / "defaultBBPs.xml";
		const auto bytes = ReadFile(path);
		if (!bytes) {
			logger::info("Default physics: no {}", path.generic_string());
			return {};
		}

		auto list = hdt::parseDefaultBBPs(*bytes);
		if (!list.valid) {
			logger::warn("Default physics: {} has no <default-bbps> root; ignored", path.generic_string());
			return list;
		}

		logger::info("Default physics: read {}: {} shape map(s), {} remap(s)", path.generic_string(), list.files.size(), list.remaps.size());
		std::error_code ec;
		for (const auto& [shape, file] : list.files) {
			// FSMP looks a physics file up from Data first, then in its own config folder.
			const bool found = std::filesystem::exists(std::filesystem::path{ "Data" } / file, ec) || std::filesystem::exists(Dir() / file, ec);
			logger::info("Default physics: shape '{}' -> {} ({})", shape, file, found ? "found" : "not found");
		}
		return list;
	}
}
