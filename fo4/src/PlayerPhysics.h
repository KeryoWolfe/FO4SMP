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
	//
	// Milestone 4: also NPCs. The attach hook names every actor whose outfit or body carries a physics XML;
	// each second the nearest of them with 3D loaded, within 2,000 units, up to smp.maximumActiveSkeletons
	// minus one, are chosen and get the same treatment from their attaches. NPCs no longer chosen are dropped.
	void Update(const hdt::DefaultBBPList& a_defaults);

	// smp.maximumActiveSkeletons: the player plus up to this many minus one NPCs.
	void SetMaxActive(int a_maxActive);

	// Times the simulated bones were all put back on their rest pose after an explosion, since the last call.
	std::uint32_t TakeSystemResets();

	// One line for the console: what's simulated on the player.
	[[nodiscard]] std::string Status();

	// Held by the frame hook around Update and the physics step; the functions below take it too.
	std::recursive_mutex& Lock();

	// Milestone 5: DynamicHDT (Papyrus.cpp). Thread-safe (they take the same lock as Update). Paths are as NIFs
	// name them (Meshes\...\x.xml); compared without case.
	[[nodiscard]] bool HasPhysics(std::uint32_t a_formID);    // the actor has at least one physics system
	[[nodiscard]] bool IsPlacedOnly(std::uint32_t a_formID);  // selected, but only placed (not simulated)
	// Each bone named, in every system of the actor: dynamic (a_on) or kinematic. Returns each name's previous
	// state (true = was dynamic; false = kinematic or not found).
	std::vector<bool> ToggleBones(std::uint32_t a_formID, const std::vector<std::string>& a_names, bool a_on);
	// Full: the actor's physics is rebuilt from the rest pose. Soft: motion cleared, pose kept. False if none.
	bool Reset(std::uint32_t a_formID, bool a_full);
	// The physics XML an attached model (ArmorAddon model paths, relative to Meshes\) names: as attached, and
	// with this actor's override applied. "" if no such attach has physics.
	[[nodiscard]] std::string AttachedPhysicsFile(std::uint32_t a_formID, const std::vector<std::string>& a_models);
	[[nodiscard]] std::string CurrentPhysicsFile(std::uint32_t a_formID, const std::vector<std::string>& a_models);
	// Use a_new where this actor's NIFs name a_original (a_new == a_original removes it); the actor is rebuilt.
	bool SetPhysicsFile(std::uint32_t a_formID, const std::string& a_original, const std::string& a_new, bool a_persist);
	[[nodiscard]] std::string OriginalPhysicsFile(std::uint32_t a_formID, const std::string& a_current);
	[[nodiscard]] bool UsesPhysicsFile(std::uint32_t a_formID, const std::string& a_path);

	struct PersistedOverride
	{
		std::uint32_t formID = 0;
		std::string original;
		std::string path;
	};
	[[nodiscard]] std::vector<PersistedOverride> PersistedOverrides();
	void RestoreOverrides(const std::vector<PersistedOverride>& a_overrides);  // replaces all (save loaded)

	// Milestone 5: MCM settings; only the values present are applied.
	struct Settings
	{
		std::optional<bool> npcPhysics;
		std::optional<bool> facePhysics;
		std::optional<int> maxNpcs;
		std::optional<float> npcDistance;
	};
	void ApplySettings(const Settings& a_settings);
}
