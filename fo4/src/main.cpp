// FO4SMP: Faster HDT-SMP for Fallout 4. GPL-3 with the modding exceptions (see ../../LICENSE, ../../EXCEPTIONS).
//
// Milestone 1: load on 1.11.240, log, check the addresses the hooks will use,
// find the config folder, and warn if FO4 Faster HDT-SMP is loaded too.
// No hooks are installed and no physics runs yet.

#include "AddressCheck.h"

namespace
{
	constexpr auto kConfigFolder = "Data/F4SE/Plugins/FO4SMP"sv;

	// Other SMP plugins this one must not run alongside.
	constexpr std::array kOtherSMP{
		L"FO4FasterHdtSMP-AVX2.dll"sv,
		L"FO4FasterHdtSMP.dll"sv,
	};

	void InitLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= std::format("{}.log", Version::PROJECT);

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		log->set_pattern("[%H:%M:%S.%e] [%l] %v");
		spdlog::set_default_logger(std::move(log));
	}

	void CheckOtherSMP()
	{
		for (const auto name : kOtherSMP) {
			if (REX::W32::GetModuleHandleW(name.data())) {
				std::string narrow;
				for (const wchar_t c : name) {
					narrow.push_back(static_cast<char>(c));  // the names are ASCII
				}
				logger::warn("Another SMP plugin is loaded: {}. Run only one SMP plugin; disable {} or FO4SMP.", narrow, narrow);
			}
		}
	}

	void CheckConfigFolder()
	{
		const std::filesystem::path folder{ kConfigFolder };
		std::error_code ec;
		if (!std::filesystem::is_directory(folder, ec)) {
			logger::warn("Config folder {} not found (the physics configs will live there)", kConfigFolder);
			return;
		}
		for (const auto file : { "configs.xml"sv, "defaultBBPs.xml"sv }) {
			const auto path = folder / file;
			logger::info("Config: {} {}", path.generic_string(), std::filesystem::exists(path, ec) ? "found" : "missing");
		}
	}

	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostLoad:
			CheckOtherSMP();
			break;
		case F4SE::MessagingInterface::kGameDataReady:
			logger::info("Game data ready");
			CheckConfigFolder();
			break;
		default:
			break;
		}
	}
}

F4SE_EXPORT constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};
	data.PluginVersion({ Version::MAJOR, Version::MINOR, Version::PATCH });
	data.PluginName(Version::PROJECT);
	data.AuthorName("KeryoWolfe");
	data.UsesAddressLibrary(true);
	data.IsLayoutDependent(true);
	data.CompatibleVersions({ REL::Version(1, 11, 240, 0) });
	return data;
}();

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, false);
	InitLog();

	const auto runtime = REL::Module::get().version();
	logger::info("{} {} loading on Fallout 4 {}", Version::PROJECT, Version::NAME, runtime.string("."sv));

	if (runtime != REL::Version(1, 11, 240, 0)) {
		logger::error("Unsupported runtime {}: this build supports 1.11.240 only. Not loading.", runtime.string("."sv));
		return false;
	}

	if (!FO4SMP::AddressCheck::Run()) {
		logger::error("Address check failed; FO4SMP will not install any hooks on this runtime.");
		// Still loaded: milestone 1 installs none. Later milestones return false here.
	} else {
		logger::info("Address check passed");
	}

	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		logger::error("Could not register for F4SE messages");
		return false;
	}

	logger::info("{} loaded", Version::PROJECT);
	return true;
}
