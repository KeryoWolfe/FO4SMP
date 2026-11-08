// FO4SMP: Faster HDT-SMP for Fallout 4. GPL-3 with the modding exceptions (see ../../LICENSE, ../../EXCEPTIONS).
//
// Milestone 1: on 1.11.240 the plugin loads, logs, checks the addresses later milestones hook,
// links FSMP's physics core and creates its world, reads configs.json and defaultBBPs.xml,
// steps the empty world once, and reports its version in the console.
// Milestone 2: one hook, on the main loop's call to Main::OnIdle, steps the (still empty)
// physics world every frame and logs its timing. Nothing is attached to actors yet.

#include "AddressCheck.h"
#include "Config.h"
#include "Console.h"
#include "FrameHook.h"
#include "PhysicsWorld.h"

namespace
{
	// Other SMP plugins this one must not run alongside.
	constexpr std::array kOtherSMP{
		L"FO4FasterHdtSMP-AVX2.dll"sv,
		L"FO4FasterHdtSMP.dll"sv,
	};

	FO4SMP::AddressCheck::Result g_addresses{};
	bool g_worldOK = false;
	bool g_reportedInConsole = false;

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

	// Creates the physics world, applies configs.json, reads defaultBBPs.xml and steps the empty world once.
	void StartPhysics()
	{
		const auto config = FO4SMP::Config::Load();
		FO4SMP::Config::Log(config);
		// logLevel isn't applied in milestone 1: the shipped value (0, fatal only) would hide this test's own lines.

		const auto defaults = FO4SMP::Config::LoadDefaultBBPs();

		try {
			auto* world = FO4SMP::PhysicsWorld::Get();
			world->Apply(config);
			logger::info("Physics world created: {} solver iterations, erp {}, time tick {:.4f} s ({} fps), up to {} substeps, wind {}",
				world->NumIterations(), world->Erp(), world->TimeTick(), config.minFps, world->MaxSubSteps(), world->m_enableWind ? "on" : "off");

			const double ms = world->SmokeStep();
			logger::info("Physics world: one empty step took {:.3f} ms", ms);
			g_worldOK = true;
			FO4SMP::FrameHook::EnableStepping(defaults);
		} catch (const std::exception& e) {
			logger::critical("Physics world could not start: {}", e.what());
		} catch (...) {
			logger::critical("Physics world could not start (unknown exception)");
		}
	}

	void ReportInConsole()
	{
		if (g_reportedInConsole) {
			return;
		}
		const auto line = std::format("{} {}: {}, physics core {}, frame step {}. Player physics builds a second after the 3D loads (milestone 3).",
			Version::PROJECT, Version::NAME,
			g_addresses.Passed() ?
				(g_addresses.hooked ? std::format("addresses OK ({} hooked by another plugin)", g_addresses.hooked) : "addresses OK"s) :
				"ADDRESS CHECK FAILED (see FO4SMP.log)"s,
			g_worldOK ? "running" : "FAILED (see FO4SMP.log)",
			FO4SMP::FrameHook::Installed() ? "hooked" : "NOT HOOKED (see FO4SMP.log)");
		if (FO4SMP::Console::Print(line)) {
			g_reportedInConsole = true;
			logger::info("Console: printed \"{}\"", line);
		} else {
			logger::warn("Console: not available yet");
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
			StartPhysics();
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			ReportInConsole();
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

	g_addresses = FO4SMP::AddressCheck::Run();
	if (g_addresses.Passed()) {
		logger::info("Address check passed");
		FO4SMP::FrameHook::Install();
	} else {
		logger::error("Address check failed; no hooks installed on this runtime.");
	}

	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		logger::error("Could not register for F4SE messages");
		return false;
	}

	logger::info("{} loaded", Version::PROJECT);
	return true;
}
