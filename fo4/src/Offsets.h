#pragma once

// Fallout 4 addresses, as Address Library IDs for each runtime FO4SMP supports, in commonlib-shared's order:
// { OG 1.10.163, NG 1.10.984, AE 1.11.240 }. REL::ID picks the running one (REX::FModule::GetRuntimeIndex).
//
// AE: each function was matched between the 1.10.163 and 1.11.240 executables (fo4/ADDRESSES.md); leads came from
// the 2020 port (ericncream/hdtSMP64) and FO4 Faster HDT-SMP (Bingle: names and ID numbers only).
// OG: the GOG 1.10.163 executable (same layout as Steam's) with Address Library 1.10.163; F4SE 0.6.23's addresses
//   (ianpatt/f4se 4d7ba86) for what came from F4SE. Each OG function's first bytes are below (from that exe).
// NG: Address Library 1.10.984 has every AE function ID used here at the address F4SE 0.7.2 (7c1bf3e) names, and
//   27 of the functions have the same size and chunk count as in AE (unwind table). The NG executable's code is
//   encrypted on disk (Steam DRM), so NG first bytes aren't known offline: in game the check uses AE's and the
//   unwind table instead (AddressCheck.cpp). Data IDs new in AE (47xxxxx) have their NG IDs (F4SE 0.7.2 = CommonLibF4).
//
// AddressCheck.cpp checks every entry in game at load, for the runtime it runs on.

namespace FO4SMP::Offsets
{
	// BSFaceGenNiNode's vtable. Matched by its RTTI name in both executables;
	// Address Library 1.11.240 keeps the 1.10.163 ID for it.
	inline constexpr REL::ID BSFaceGenNiNode_vtbl{ 539845 };  // the same ID in all three

	// BSFaceGenNiNode::SkinAllGeometry: the vtable's last entry, slot 0x43, in
	// both versions. FSMP hooks this for head parts (hair). In 1.11.240 the
	// per-geometry call it makes in 1.10.163 (SkinSingleGeometry) is gone,
	// inlined into this function, so FSMP's second hook has no target here.
	inline constexpr std::size_t   BSFaceGenNiNode_SkinAllGeometry_slot = 0x43;
	inline constexpr REL::ID       BSFaceGenNiNode_SkinAllGeometry{ 1131949, 2209478, 2209478 };

	// TESNPC::GetFaceGeomPath. The only function referencing the string
	// "Meshes\Actors\Character\FaceGenData\FaceGeom\%s\%08X.NIF" in either
	// version; same size (0xB8) and same code in both.
	inline constexpr REL::ID TESNPC_GetFaceGeomPath{ 1135980, 2207453, 2207453 };

	// The console log's singleton pointer (data). CommonLibF4 names next-gen ID 2690148 for it, which
	// Address Library 1.11.240 doesn't have; 4797437 is the pointer the game loads into rcx before
	// 976 calls to ConsoleLog::Print / AddString (ADDRESSES.md).
	inline constexpr REL::ID ConsoleLog_Singleton{ 689441, 2690148, 4797437 };

	// The game's main loop, and in it the call to Main::OnIdle (the frame hook's site, milestone 2).
	// Found in 1.11.240: the only caller of Main::OnIdle; `mov rcx, [Main singleton]; call OnIdle`
	// once per loop iteration.
	inline constexpr REL::ID MainLoop{ 1125396, 2718225, 4484191 };  // Main::Run; the call is at +0xBB in OG, +0xCB in AE, found at load
	inline constexpr REL::ID MainOnIdle{ 633524, 2228917, 2228917 };

	// The player (PlayerCharacter*): F4SE's g_player for 1.11.240, 0x032DD370 (f4se/GameReferences.cpp at 6f6a7ca),
	// which is Address Library 2698073; referenced from 3,967 places in the executable. CommonLibF4's ID for it
	// (2690919) isn't in Address Library 1.11.240.
	inline constexpr REL::ID PlayerCharacter_Singleton{ 303410, 2698073, 2698073 };  // F4SE g_player: OG 0x05AA4388, NG 0x0303ACA0

	// Main's static "game systems should update" flag (CommonLibF4 Main::QGameSystemsShouldUpdate, ID 2698031 =
	// +0x32DD0FA in 1.11.240, the byte before QGameDataLoaded's 2698032). Main itself is OnIdle's `this`: freezeTime
	// at +0x2A, inMenuMode at +0x1D0 (CommonLibF4 Main.h). Used to stop stepping while the game is paused.
	inline constexpr REL::ID Main_GameSystemsShouldUpdate{ 779552, 2698031, 2698031 };  // OG: the byte before QGameDataLoaded, as in AE

	// The game timer (CommonLibF4 BSTimer::GetSingleton, ID 2703179 = +0x339F420 in 1.11.240): delta (game time,
	// +0x10), realTimeDelta (+0x14), disableCounter (+0x38). The pause menu doesn't clear "game systems should
	// update" (run 39), so the pause is read from the timer.
	inline constexpr REL::ID BSTimer_Singleton{ 1256126, 2703179, 2703179 };

	// UI* (F4SE 1.11.240 GameMenus.cpp g_ui, 0x030E8930 = Address Library 4796314; 632 references in the executable).
	// UI::numPauseGame at +0x1E0 (F4SE GameMenus.h; CommonLibF4 UI::menuMode, same offset): the count of open menus that
	// pause the game, which F4SE's own input hooks test. The timer's delta didn't drop in the pause menu (run 40).
	inline constexpr REL::ID UI_Singleton{ 548587, 2689028, 4796314 };  // F4SE g_ui: OG 0x058D0898, NG 0x02E66400

	// PlayerCamera* (F4SE 1.11.240 GameCamera.cpp g_playerCamera 0x030E6E58 = ID 4796065; Dear Modding's CommonLibF4
	// has the same AE id). TESCamera::cameraNode at +0x20 (F4SE GameCamera.h). Culling (milestone 4).
	inline constexpr REL::ID PlayerCamera_Singleton{ 1171980, 2688801, 4796065 };  // OG 0x058CEB28, NG 0x02E649D8

	// Milestone 5, per-actor bone nodes. F4SE 1.11.240 (GameAPI.cpp, NiNodes.h/.cpp): the main heap object
	// g_mainHeap 0x03E74AB0 = ID 4807654; Heap::Allocate(size, alignment, aligned) 0x01657DE0 = ID 2267872;
	// NiNode::NiNode(UInt16 children) 0x016BE500 = ID 2269948. Checked in the executable: the game itself
	// allocates NiNodes as Allocate(&mainHeap, 0x140, 0x10, true) then calls that constructor (120 call sites).
	inline constexpr REL::ID MainHeap{ 42693, 2688720, 4807654 };        // OG 0x038CC980, NG 0x02E62980
	inline constexpr REL::ID HeapAllocate{ 652767, 2267872, 2267872 };   // OG 0x01B0EFD0
	inline constexpr REL::ID HeapFree{ 1582181, 2267874, 2267874 };  // Heap::Free(buffer, aligned), F4SE 1.11.240 0x01658240
	inline constexpr REL::ID NiNodeCtor{ 20633, 2269948, 2269948 };      // OG 0x01B98920; sizeof(NiNode) 0x140 in all three

	// TESDataHandler* (F4SE 1.11.240 g_dataHandler 0x030E7100 = ID 4796135); its form arrays start at +0x68
	// (CommonLibF4 and F4SE agree), BSTArray<BGSHeadPart*> is form type 15: +0x1D0. BGSHeadPart: model
	// (TESModel at +0x98, its path at +0xA0), editor ID / part name (BSFixedString at +0x170), which the face
	// generator names each head part's shape after.
	inline constexpr REL::ID DataHandler_Singleton{ 711558, 2688883, 4796135 };  // OG 0x058CF080, NG 0x02E64E68

	// Milestone 5, files in BA2 archives. F4SE 1.11.240 GameStreams.h: BSResourceNiBinaryStream (0x30 bytes)
	// Construct(path, 0, 0, 0) 0x016B9C40 = ID 2269830, its destructor 0x016B9F30 = ID 2269832, IsValid 0x016BA070 =
	// ID 2269833, Read(buffer, length) 0x016BA230 = ID 2269839 (returns the bytes read). Checked in the executable:
	// the constructor and destructor both store the BSResourceNiBinaryStream vtable; Read adds the count to +0x20.
	inline constexpr REL::ID ResourceStreamCtor{ 1198116, 2269830, 2269830 };  // OG 0x01B93740
	inline constexpr REL::ID ResourceStreamDtor{ 1516202, 2269832, 2269832 };
	inline constexpr REL::ID ResourceStreamIsValid{ 1502562, 2269833, 2269833 };
	inline constexpr REL::ID ResourceStreamRead{ 424286, 2269839, 2269839 };

	// Armor attach (milestone 4). BipedAnim::ApplySkinnedObjects calls BipedAnim::AttachSkinnedObject from two
	// places (1.11.240: +0x35DDB2 and +0x35DDDB, inside a chained chunk of the function); both are `call rel32`.
	// FO4SMP hooks those calls, not the function: LooksMenu (f4ee.dll) detours the function itself, and a call
	// site hook calls through whatever is there. Signature from F4SE 1.11.240's GameFormComponents.h (same
	// address, 0x0035E960): NiNode* AttachSkinnedObject(BipedAnim*, NiNode* objectRoot, NiNode* parent,
	// UInt32 bipedIndex, bool firstPerson); checked against both call sites (rdx object, r8 parent or null,
	// r9d index, byte on the stack).
	//
	// The call sites are found at load (AttachHook.cpp): every `call AttachSkinnedObject` in the code of the functions
	// below, through the unwind table's chunks. AE and NG: ApplySkinnedObjects, two calls. OG: one call there, and one
	// in a helper ApplySkinnedObjects calls for an object's children (ID 973253, +0x1C6420), which AE inlines.
	inline constexpr REL::ID BipedAnimApplySkinnedObjects{ 224320, 2194381, 2194381 };
	inline constexpr REL::ID BipedAnimApplySkinnedObjectsChildren{ 973253, 0, 0 };  // OG only (0: none)
	inline constexpr REL::ID BipedAnimAttachSkinnedObject{ 1575810, 2194388, 2194388 };

	// bool LookupREFRByHandle(const UInt32& handle, NiPointer<TESObjectREFR>& out): F4SE 1.11.240's
	// GameHandle.cpp, 0x002A0860 = Address Library 2190940 (reads the handle, masks index 0x1FFFFF and age).
	// Resolves BipedAnim::actorRef (+0x1E50); the biped root's userData isn't set yet when outfits attach (run 29).
	inline constexpr REL::ID LookupREFRByHandle{ 542603, 2190940, 2190940 };  // OG 0x0000AB60, NG 0x0024C240

	struct Function
	{
		std::string_view              name;
		REL::ID                       id;       // { OG, NG, AE }
		std::array<std::uint16_t, 8>  headOG;   // first bytes in 1.10.163 (0x100: any byte)
		std::array<std::uint8_t, 8>   head;     // first bytes in 1.11.240 (NG: checked against these too)
		bool                          strong;   // false: matched less firmly, see ADDRESSES.md
	};

	// Game functions FSMP needs, by role:
	//   frame step: MainOnIdle; armor and body: BipedAnim*; skinning: BSSkinInstance*, BSFlattenedBoneTree*;
	//   head parts: BSFaceGen*, TESNPC*, TESObjectREFRFixDisplayedHeadParts; culling: ActorCalculateLOS;
	//   collision: ActorGetCollisionFilter; resets: Reset3D, FirstThirdPersonSwitchEventSource.
	inline constexpr std::array kFunctions{
		Function{ "ActorCalculateLOS"sv, REL::ID{ 1324305, 2230201, 2230201 }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74 }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C }, true },
		Function{ "ActorGetCollisionFilter"sv, REL::ID{ 1474995, 2229991, 2229991 }, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B }, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B }, true },
		Function{ "BSFaceGenAddHeadPartOnActor"sv, REL::ID{ 913780, 2209545, 2209545 }, { 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x5B, 0x01, 0x00 }, { 0x48, 0x85, 0xC9, 0x0F, 0x84, 0xB2, 0x01, 0x00 }, true },
		Function{ "BSFaceGenModelExtraDataSetBoneName"sv, REL::ID{ 1278503, 2209387, 2209387 }, { 0x8B, 0xC2, 0x49, 0x8B, 0xD0, 0x48, 0x8D, 0x0C }, { 0x8B, 0xC2, 0x49, 0x8B, 0xD0, 0x48, 0x8D, 0x0C }, true },
		Function{ "BSFaceGenPrepareHeadPart"sv, REL::ID{ 840416, 2209534, 2209534 }, { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x41, 0x56 }, { 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x41, 0x56 }, true },
		Function{ "BSFlattenedBoneTreeFind"sv, REL::ID{ 1435518, 2270080, 2270080 }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C }, true },
		Function{ "BSFlattenedBoneTreeGetOrCreateBoneNode"sv, REL::ID{ 865291, 2270071, 2270071 }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83 }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83 }, true },
		Function{ "BSSkinInstanceGetFlattenedBoneReference"sv, REL::ID{ 73083, 2270469, 2270469 }, { 0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x50 }, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57 }, true },
		Function{ "BipedAnimApplySkinnedObjects"sv, REL::ID{ 224320, 2194381, 2194381 }, { 0x44, 0x88, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44 }, { 0x44, 0x88, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44 }, true },
		Function{ "BipedAnimAttachSkinnedObject"sv, REL::ID{ 1575810, 2194388, 2194388 }, { 0x44, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44 }, { 0x44, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44 }, true },
		Function{ "BipedAnimAttachToParent"sv, REL::ID{ 1370428, 2194378, 2194378 }, { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x4C }, { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x4C }, true },
		Function{ "BipedAnimRemovePart"sv, REL::ID{ 575576, 2194342, 2194342 }, { 0x48, 0x8B, 0xC4, 0x44, 0x88, 0x40, 0x18, 0x48 }, { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x48 }, true },
		Function{ "FirstThirdPersonSwitchEventSource"sv, REL::ID{ 930195, 2233725, 2233725 }, { 0x48, 0x83, 0xEC, 0x28, 0x8B, 0x05, 0x100, 0x100 }, { 0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04 }, false },
		Function{ "LookupREFRByHandle"sv, REL::ID{ 542603, 2190940, 2190940 }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C }, true },
		Function{ "HeapAllocate"sv, REL::ID{ 652767, 2267872, 2267872 }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C }, true },
		Function{ "NiNodeCtor"sv, REL::ID{ 20633, 2269948, 2269948 }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 }, true },
		Function{ "ResourceStreamCtor"sv, REL::ID{ 1198116, 2269830, 2269830 }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C }, true },
		Function{ "ResourceStreamDtor"sv, REL::ID{ 1516202, 2269832, 2269832 }, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B }, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B }, true },
		Function{ "ResourceStreamRead"sv, REL::ID{ 424286, 2269839, 2269839 }, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x33, 0xC0 }, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x33, 0xC0 }, true },
		Function{ "LooksMenuUtilsShowLooksMenu"sv, REL::ID{ 411372, 2223366, 2223366 }, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83 }, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83 }, true },
		Function{ "MainOnIdle"sv, REL::ID{ 633524, 2228917, 2228917 }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 }, { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48 }, true },
		Function{ "Reset3D"sv, REL::ID{ 302888, 2229913, 2229913 }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C }, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C }, true },
		Function{ "TESNPCGetAlternateHeadPartList"sv, REL::ID{ 437572, 2207501, 2207501 }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 }, true },
		Function{ "TESNPCGetAlternateHeadPartListSize"sv, REL::ID{ 517331, 2207502, 2207502 }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 }, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 }, true },
		Function{ "ConsoleLogPrint"sv, REL::ID{ 1065300, 2248591, 2248591 }, { 0x48, 0x89, 0x54, 0x24, 0x10, 0x4C, 0x89, 0x44 }, { 0x48, 0x89, 0x54, 0x24, 0x10, 0x4C, 0x89, 0x44 }, true },
		Function{ "TESObjectREFRFixDisplayedHeadParts"sv, REL::ID{ 789414, 2200988, 2200988 }, { 0x40, 0x53, 0x56, 0x41, 0x54, 0x48, 0x81, 0xEC }, { 0x40, 0x53, 0x55, 0x56, 0x41, 0x54, 0x48, 0x83 }, true },
	};
}
