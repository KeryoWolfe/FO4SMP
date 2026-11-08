#pragma once

namespace hdt
{
	struct DefaultBBPList;
}

namespace FO4SMP::PlayerPhysics
{
	// Milestone 3: SMP on the player's body, without armor hooks.
	//
	// Each frame (from the frame hook, before the physics step) this checks the player's third-person 3D.
	// When it changes (a save loads, the player's 3D is rebuilt) the old physics is dropped and, a second
	// later, the new 3D is scanned: every node carrying "HDT Skinned Mesh Physics Object" (or every skinned
	// shape defaultBBPs.xml names) gets an FSMP system built from that physics XML. While systems exist,
	// they're dropped and rebuilt if the shape they were built from is no longer attached (the body or
	// outfit changed).
	void Update(const hdt::DefaultBBPList& a_defaults);

	// One line for the console: what's simulated on the player.
	[[nodiscard]] std::string Status();
}
