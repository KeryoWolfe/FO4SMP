#pragma once

namespace FO4SMP::FrameHook
{
	// Milestone 2: steps the physics world once per frame from the game's main loop.
	//
	// The game's main loop (Address Library 4484191, 1.11.240) calls Main::OnIdle (2228917) once per
	// iteration, at +0xCB. FO4SMP redirects that one call to its own function, which calls whatever
	// the call pointed at before (Main::OnIdle, or another plugin's hook on it) and then steps
	// physics. Installed at plugin load; refuses if the call site isn't a call.
	bool Install();

	// Steps start once the physics world exists (game data ready).
	void EnableStepping();

	[[nodiscard]] bool Installed() noexcept;
}
