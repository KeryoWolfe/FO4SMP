#include "FrameHook.h"
#include "game/Fo4Bone.h"

#include "Offsets.h"
#include "PhysicsWorld.h"
#include "PlayerPhysics.h"
#include "Settings.h"
#include "hdtDefaultBBPList.h"

namespace FO4SMP::FrameHook
{
	namespace
	{
		using OnIdle_t = void(void*);

		OnIdle_t* g_onIdle = nullptr;  // what the call pointed at before us
		bool g_installed = false;
		std::atomic<bool> g_stepping = false;
		hdt::DefaultBBPList g_defaults;
		bool g_frozen = false;

		// Per-frame timing, reported every kReportSeconds.
		constexpr double kReportSeconds = 10.0;
		constexpr float kMaxFrameSeconds = 0.25f;  // a longer frame (a hitch, a load) steps as this much at most
		std::chrono::steady_clock::time_point g_lastFrame{};
		std::chrono::steady_clock::time_point g_reportStart{};
		std::uint64_t g_frames = 0;
		double g_stepMsTotal = 0.0;
		double g_stepMsMax = 0.0;

		void Report(std::chrono::steady_clock::time_point a_now)
		{
			const double span = std::chrono::duration<double>(a_now - g_reportStart).count();
			if (span < (logger::Verbose() ? kReportSeconds : 6 * kReportSeconds)) {  // a minute apart unless verbose
				return;
			}
			if (g_frames > 0) {
				logger::info("Frame step: {} frames in {:.1f} s ({:.1f} fps); physics step average {:.3f} ms, longest {:.3f} ms{}{}",
					g_frames, span, g_frames / span, g_stepMsTotal / g_frames, g_stepMsMax, hdt::Fo4Bone::TakeWriteReport(), PhysicsWorld::Get()->TakeLoadReport());
				if (const auto resets = PlayerPhysics::TakeSystemResets()) {
					logger::info("Player physics: simulated bones put back on their rest pose {} time(s) after an explosion", resets);
				}
				if (const auto contacts = PhysicsWorld::Get()->TakeContactReport(); !contacts.empty()) {
					logger::info("Contacts: {}", contacts);
				}
			}
			g_reportStart = a_now;
			g_frames = 0;
			g_stepMsTotal = 0.0;
			g_stepMsMax = 0.0;
		}

		// The game is paused (pause menu, Pip-Boy, loading): no physics step, so bodies don't keep moving under a frozen
		// game (user, run 38). Logs each change, with the flags read, to confirm which flag follows the pause.
		bool Paused(void* a_main)
		{
			static REL::Relocation<bool*> shouldUpdate{ Offsets::Main_GameSystemsShouldUpdate };
			static REL::Relocation<std::uintptr_t> timer{ Offsets::BSTimer_Singleton };
			const auto main = reinterpret_cast<std::uintptr_t>(a_main);
			const bool freezeTime = main && *reinterpret_cast<const bool*>(main + 0x2A);
			const bool update = *shouldUpdate;
			const float delta = *reinterpret_cast<const float*>(timer.address() + 0x10);
			const float realDelta = *reinterpret_cast<const float*>(timer.address() + 0x14);
			const auto disabled = *reinterpret_cast<const std::uint32_t*>(timer.address() + 0x38);
			static REL::Relocation<std::uintptr_t*> ui{ Offsets::UI_Singleton };
			const auto uiObject = *ui;
			const std::uint32_t pauseMenus = uiObject ? *reinterpret_cast<const std::uint32_t*>(uiObject + 0x1E0) : 0;
			const bool paused = !update || freezeTime || disabled > 0 || !(delta > 0.0f) || pauseMenus > 0;
			static bool s_paused = false;
			static int s_logged = 0;
			if (paused != s_paused && s_logged < 60) {
				++s_logged;
				logger::info("Frame step: {} (game systems update {}, freeze time {}, timer delta {:.4f} s, real {:.4f} s, timer disabled {}, pausing menus {})",
					paused ? "paused" : "resumed", update, freezeTime, delta, realDelta, disabled, pauseMenus);
			}
			s_paused = paused;
			return paused;
		}

		void Step(void* a_main)
		{
			const auto now = std::chrono::steady_clock::now();
			Settings::Poll();  // MCM changes are made while paused
			PlayerPhysics::CheckFaceEditor();  // before the pause check: LooksMenu pauses the game
			if (Paused(a_main)) {
				g_lastFrame = now;  // no time passes for the physics while paused
				return;
			}
			if (g_lastFrame.time_since_epoch().count() == 0) {
				g_lastFrame = now;
				g_reportStart = now;
				logger::info("Frame step: first frame seen; stepping from the next one");
				return;
			}

			const float frameSeconds = std::min(std::chrono::duration<float>(now - g_lastFrame).count(), kMaxFrameSeconds);
			g_lastFrame = now;

			// The frame's work and the step under PlayerPhysics' lock, against DynamicHDT script calls.
			std::scoped_lock guard{ PlayerPhysics::Lock() };
			PlayerPhysics::Update(g_defaults);

			// Diagnostic switch: with F4SE\Plugins\FO4SMP\freeze.txt present, the physics is built but not
			// stepped, so the body shows the rest pose FO4SMP built it from.
			const double ms = g_frozen ? 0.0 : PhysicsWorld::Get()->Step(frameSeconds);
			++g_frames;
			g_stepMsTotal += ms;
			g_stepMsMax = std::max(g_stepMsMax, ms);

			Report(now);
		}

		void OnIdle_Hook(void* a_main)
		{
			g_onIdle(a_main);

			if (!g_stepping.load(std::memory_order_acquire)) {
				return;
			}
			try {
				Step(a_main);
			} catch (const std::exception& e) {
				g_stepping = false;
				logger::critical("Frame step: stopped after an exception: {}", e.what());
			} catch (...) {
				g_stepping = false;
				logger::critical("Frame step: stopped after an unknown exception");
			}
		}
	}

	bool Install()
	{
		const REL::Relocation<std::uintptr_t> site{ Offsets::MainLoop, Offsets::MainLoop_OnIdleCall };
		const auto* code = reinterpret_cast<const std::uint8_t*>(site.address());
		if (code[0] != 0xE8) {
			logger::error("Frame hook: the main loop's call to Main::OnIdle (+{:07X}) isn't a call (starts {:02X}); not installed",
				site.address() - REX::FModule::GetExecutingModule().GetBaseAddress(), code[0]);
			return false;
		}

		std::int32_t rel;
		std::memcpy(&rel, code + 1, sizeof(rel));
		const auto current = site.address() + 5 + rel;
		const auto onIdle = Offsets::MainOnIdle.address();
		if (current != onIdle) {
			logger::warn("Frame hook: the main loop's call already points at +{:X}, not Main::OnIdle: another plugin hooked it first; FO4SMP calls it in turn",
				current - REX::FModule::GetExecutingModule().GetBaseAddress());
		}

		g_onIdle = reinterpret_cast<OnIdle_t*>(REL::GetTrampoline().write_call<5>(site.address(), &OnIdle_Hook));  // trampoline: F4SE::Init
		g_installed = true;
		logger::info("Frame hook: installed at the main loop's call to Main::OnIdle (+{:07X})", site.address() - REX::FModule::GetExecutingModule().GetBaseAddress());
		return true;
	}

	void EnableStepping(const hdt::DefaultBBPList& a_defaults)
	{
		g_defaults = a_defaults;
		std::error_code ec;
		g_frozen = std::filesystem::exists(std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "FO4SMP" / "freeze.txt", ec);
		if (g_frozen) {
			logger::info("Frame step: freeze.txt found; the physics is built but not stepped (rest pose shown)");
		}
		hdt::Fo4Bone::s_worldOnly = std::filesystem::exists(std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "FO4SMP" / "worldonly.txt", ec);
		if (hdt::Fo4Bone::s_worldOnly) {
			logger::info("Frame step: worldonly.txt found; simulated bones get their world transform only (diagnostic)");
		}
		if (g_installed) {
			g_stepping = true;
			logger::info("Frame step: enabled");
		}
	}

	bool Installed() noexcept { return g_installed; }
}
