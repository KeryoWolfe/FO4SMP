#pragma once

// Milestone 4: Fallout 4's armor attach. Hooks BipedAnim::ApplySkinnedObjects' two calls to
// BipedAnim::AttachSkinnedObject and records, for every skinned object the game attaches to an actor, the
// model path (from the biped slot's TESModel) and the physics XML its root names before it is attached
// (the "HDT Skinned Mesh Physics Object" extra data, which is gone once the object is merged into the
// skeleton). Step A only observes and logs; the physics still builds from the player scan.

#include <cstdint>
#include <string>

namespace RE
{
	class NiAVObject;
}

namespace FO4SMP::AttachHook
{
	// Installs both call-site hooks; false (and logged) if either site isn't the expected call.
	bool Install();
	bool Installed();

	struct Attach
	{
		std::uintptr_t actor = 0;      // the actor's TESObjectREFR* (biped root's userData), 0 if unknown
		std::uint32_t  formID = 0;
		std::uint32_t  bipedIndex = 0;
		bool           firstPerson = false;
		std::string    objectName;     // the object's root node name
		std::string    model;          // TESModel path of the biped slot (relative to Meshes\), "" if none
		std::string    physicsXml;     // the root's "HDT Skinned Mesh Physics Object", "" if none
		RE::NiAVObject* attached = nullptr;  // what AttachSkinnedObject returned
	};

	// Copies the most recent attach that returned a_node into a_out; false if none is recorded. Thread-safe.
	bool FindByAttachedNode(RE::NiAVObject* a_node, Attach& a_out);
}
