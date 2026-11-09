#pragma once

// Fallout 4 1.11.240 addresses, as Address Library IDs.
//
// Every entry here was found by matching the function in the 1.10.163 executable
// (where the 2020 port, github.com/ericncream/hdtSMP64, located it) against the
// 1.11.240 executable. How each was matched, and what is still unresolved, is in
// fo4/ADDRESSES.md. AddressCheck.cpp checks each one in game at load.
//
// The table at the end came from FO4 Faster HDT-SMP (Bingle, GPL-3): its PDB names
// the game functions it uses and its code holds their Address Library IDs (1.10.163
// and next-gen). Each was then checked independently in both executables (same
// shape of code, same callers among the others); see fo4/ADDRESSES.md.
//
// None of these are hooked yet (milestone 1 only checks them).

namespace FO4SMP::Offsets
{
	// BSFaceGenNiNode's vtable. Matched by its RTTI name in both executables;
	// Address Library 1.11.240 keeps the 1.10.163 ID for it.
	inline constexpr REL::ID BSFaceGenNiNode_vtbl{ 539845 };

	// BSFaceGenNiNode::SkinAllGeometry: the vtable's last entry, slot 0x43, in
	// both versions. FSMP hooks this for head parts (hair). In 1.11.240 the
	// per-geometry call it makes in 1.10.163 (SkinSingleGeometry) is gone,
	// inlined into this function, so FSMP's second hook has no target here.
	inline constexpr std::size_t   BSFaceGenNiNode_SkinAllGeometry_slot = 0x43;
	inline constexpr REL::ID       BSFaceGenNiNode_SkinAllGeometry{ 2209478 };

	// TESNPC::GetFaceGeomPath. The only function referencing the string
	// "Meshes\Actors\Character\FaceGenData\FaceGeom\%s\%08X.NIF" in either
	// version; same size (0xB8) and same code in both.
	inline constexpr REL::ID TESNPC_GetFaceGeomPath{ 2207453 };

	// The console log's singleton pointer (data). CommonLibF4 names next-gen ID 2690148 for it, which
	// Address Library 1.11.240 doesn't have; 4797437 is the pointer the game loads into rcx before
	// 976 calls to ConsoleLog::Print / AddString (ADDRESSES.md).
	inline constexpr REL::ID ConsoleLog_Singleton{ 4797437 };

	// The game's main loop, and in it the call to Main::OnIdle (the frame hook's site, milestone 2).
	// Found in 1.11.240: the only caller of Main::OnIdle; `mov rcx, [Main singleton]; call OnIdle`
	// once per loop iteration.
	inline constexpr REL::ID          MainLoop{ 4484191 };
	inline constexpr std::ptrdiff_t   MainLoop_OnIdleCall = 0xCB;
	inline constexpr REL::ID          MainOnIdle{ 2228917 };

	// The player (PlayerCharacter*): F4SE's g_player for 1.11.240, 0x032DD370 (f4se/GameReferences.cpp at 6f6a7ca),
	// which is Address Library 2698073; referenced from 3,967 places in the executable. CommonLibF4's ID for it
	// (2690919) isn't in Address Library 1.11.240.
	inline constexpr REL::ID PlayerCharacter_Singleton{ 2698073 };

	// Main's static "game systems should update" flag (CommonLibF4 Main::QGameSystemsShouldUpdate, ID 2698031 =
	// +0x32DD0FA in 1.11.240, the byte before QGameDataLoaded's 2698032). Main itself is OnIdle's `this`: freezeTime
	// at +0x2A, inMenuMode at +0x1D0 (CommonLibF4 Main.h). Used to stop stepping while the game is paused.
	inline constexpr REL::ID Main_GameSystemsShouldUpdate{ 2698031 };

	// The game timer (CommonLibF4 BSTimer::GetSingleton, ID 2703179 = +0x339F420 in 1.11.240): delta (game time,
	// +0x10), realTimeDelta (+0x14), disableCounter (+0x38). The pause menu doesn't clear "game systems should
	// update" (run 39), so the pause is read from the timer.
	inline constexpr REL::ID BSTimer_Singleton{ 2703179 };

	// Armor attach (milestone 4). BipedAnim::ApplySkinnedObjects calls BipedAnim::AttachSkinnedObject from two
	// places (1.11.240: +0x35DDB2 and +0x35DDDB, inside a chained chunk of the function); both are `call rel32`.
	// FO4SMP hooks those calls, not the function: LooksMenu (f4ee.dll) detours the function itself, and a call
	// site hook calls through whatever is there. Signature from F4SE 1.11.240's GameFormComponents.h (same
	// address, 0x0035E960): NiNode* AttachSkinnedObject(BipedAnim*, NiNode* objectRoot, NiNode* parent,
	// UInt32 bipedIndex, bool firstPerson); checked against both call sites (rdx object, r8 parent or null,
	// r9d index, byte on the stack).
	inline constexpr REL::ID        BipedAnimApplySkinnedObjects{ 2194381 };
	inline constexpr std::ptrdiff_t BipedAnimApplySkinnedObjects_AttachCall1 = 0x35DDB2 - 0x35DC20;
	inline constexpr std::ptrdiff_t BipedAnimApplySkinnedObjects_AttachCall2 = 0x35DDDB - 0x35DC20;
	inline constexpr REL::ID        BipedAnimAttachSkinnedObject{ 2194388 };

	// bool LookupREFRByHandle(const UInt32& handle, NiPointer<TESObjectREFR>& out): F4SE 1.11.240's
	// GameHandle.cpp, 0x002A0860 = Address Library 2190940 (reads the handle, masks index 0x1FFFFF and age).
	// Resolves BipedAnim::actorRef (+0x1E50); the biped root's userData isn't set yet when outfits attach (run 29).
	inline constexpr REL::ID LookupREFRByHandle{ 2190940 };

	struct Function
	{
		std::string_view              name;
		std::uint64_t                 id;        // Address Library, 1.11.240
		std::uint64_t                 id_1_10;   // Address Library, 1.10.163 (reference only)
		std::array<std::uint8_t, 8>   head;      // first bytes in 1.11.240
		bool                          strong;    // false: matched less firmly, see ADDRESSES.md
	};

	// Game functions FSMP needs, by role:
	//   frame step: MainOnIdle; armor and body: BipedAnim*; skinning: BSSkinInstance*, BSFlattenedBoneTree*;
	//   head parts: BSFaceGen*, TESNPC*, TESObjectREFRFixDisplayedHeadParts; culling: ActorCalculateLOS;
	//   collision: ActorGetCollisionFilter; resets: Reset3D, FirstThirdPersonSwitchEventSource.
	inline constexpr std::array kFunctions{
		Function{ "ActorCalculateLOS"sv, 2230201, 1324305, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C }, true },
		Function{ "ActorGetCollisionFilter"sv, 2229991, 1474995, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B }, true },
		Function{ "BSFaceGenAddHeadPartOnActor"sv, 2209545, 913780, { 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xB2, 0x01, 0x00 }, true },
		Function{ "BSFaceGenModelExtraDataSetBoneName"sv, 2209387, 1278503, { 0x8B, 0xC2, 0x49, 0x8B, 0xD0, 0x48, 0x8D, 0x0C }, true },
		Function{ "BSFaceGenPrepareHeadPart"sv, 2209534, 840416, { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x41, 0x56 }, true },
		Function{ "BSFlattenedBoneTreeFind"sv, 2270080, 1435518, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C }, true },
		Function{ "BSFlattenedBoneTreeGetOrCreateBoneNode"sv, 2270071, 865291, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83 }, true },
		Function{ "BSSkinInstanceGetFlattenedBoneReference"sv, 2270469, 73083, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57 }, true },
		Function{ "BipedAnimApplySkinnedObjects"sv, 2194381, 224320, { 0x44, 0x88, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44 }, true },
		Function{ "BipedAnimAttachSkinnedObject"sv, 2194388, 1575810, { 0x44, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44 }, true },
		Function{ "BipedAnimAttachToParent"sv, 2194378, 1370428, { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x4C }, true },
		Function{ "BipedAnimRemovePart"sv, 2194342, 575576, { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x48 }, true },
		Function{ "FirstThirdPersonSwitchEventSource"sv, 2233725, 930195, { 0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04 }, false },
		Function{ "LookupREFRByHandle"sv, 2190940, 0, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C }, true },
		Function{ "LooksMenuUtilsShowLooksMenu"sv, 2223366, 411372, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83 }, true },
		Function{ "MainOnIdle"sv, 2228917, 633524, { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48 }, true },
		Function{ "Reset3D"sv, 2229913, 302888, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C }, true },
		Function{ "TESNPCGetAlternateHeadPartList"sv, 2207501, 437572, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 }, true },
		Function{ "TESNPCGetAlternateHeadPartListSize"sv, 2207502, 517331, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 }, true },
		Function{ "ConsoleLogPrint"sv, 2248591, 799546, { 0x48, 0x89, 0x54, 0x24, 0x10, 0x4C, 0x89, 0x44 }, true },
		Function{ "TESObjectREFRFixDisplayedHeadParts"sv, 2200988, 789414, { 0x40, 0x53, 0x55, 0x56, 0x41, 0x54, 0x48, 0x83 }, true },
	};
}
