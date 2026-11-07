#pragma once

// Fallout 4 1.11.240 addresses, as Address Library IDs.
//
// Every entry here was found by matching the function in the 1.10.163 executable
// (where the 2020 port, github.com/ericncream/hdtSMP64, located it) against the
// 1.11.240 executable. How each was matched, and what is still unresolved, is in
// fo4/ADDRESSES.md. AddressCheck.cpp checks each one in game at load.
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
}
