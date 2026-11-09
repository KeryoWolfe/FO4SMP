#include "AttachHook.h"

#include "Offsets.h"
#include "game/Fo4NiUtils.h"

namespace FO4SMP::AttachHook
{
	namespace
	{
		using Attach_t = RE::NiNode*(void* a_biped, RE::NiNode* a_object, RE::NiNode* a_parent, std::uint32_t a_bipedIndex, bool a_firstPerson);

		Attach_t* g_original1 = nullptr;
		Attach_t* g_original2 = nullptr;
		bool      g_installed = false;
		std::unordered_map<std::uint32_t, PhysicsActor> g_actors;  // under g_lock
		std::unordered_map<std::uint32_t, std::uint32_t> g_seen;    // every actor with a third-person attach: form ID -> handle; under g_lock
		// The latest third-person physics attach per actor and biped slot, kept for good (g_recent is bounded, and
		// with 40 settlers around, Cait's body attach had dropped out of it by the time she was rebuilt indoors, run 34).
		std::unordered_map<std::uint32_t, std::map<std::uint32_t, Attach>> g_physicsBySlot;  // under g_lock

		std::mutex          g_lock;
		std::vector<Attach> g_recent;  // newest last; bounded
		constexpr std::size_t kMaxRecent = 256;
		int g_logged = 0;
		constexpr int kMaxLogged = 1500;

		// BipedAnim (CommonLibF4 TESObjectREFRs.h, 0x1E58; matches F4SE): root at +0x08, BIPOBJECT[44] at +0x10
		// (0x58 each; TESModel* part at +0x20 in each). Read raw so a layout mismatch can't call into anything.
		constexpr std::size_t kBipedObjects = 44;
		constexpr std::size_t kBipObjectSize = 0x58;

		std::string ModelPath(void* a_biped, std::uint32_t a_index)
		{
			if (!a_biped || a_index >= kBipedObjects) {
				return {};
			}
			const auto base = reinterpret_cast<std::uintptr_t>(a_biped) + 0x10 + a_index * kBipObjectSize;
			const auto part = *reinterpret_cast<const RE::TESModel* const*>(base + 0x20);
			if (!part) {
				return {};
			}
			const char* model = part->model.c_str();
			return model ? std::string{ model } : std::string{};
		}

		using Lookup_t = bool(const std::uint32_t&, RE::NiPointer<RE::TESObjectREFR>&);

		void ActorOf(void* a_biped, std::uintptr_t& a_actor, std::uint32_t& a_formID, std::uint32_t& a_handle)
		{
			a_actor = 0;
			a_formID = 0;
			a_handle = 0;
			if (!a_biped) {
				return;
			}
			// BipedAnim::actorRef, an ObjectRefHandle (+0x1E50), resolved by the game's own lookup.
			const auto handle = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(a_biped) + 0x1E50);
			if (!handle) {
				return;
			}
			a_handle = handle;
			static REL::Relocation<Lookup_t*> lookup{ Offsets::LookupREFRByHandle };
			RE::NiPointer<RE::TESObjectREFR> ref;
			if (lookup(handle, ref) && ref) {
				a_actor = reinterpret_cast<std::uintptr_t>(ref.get());
				a_formID = ref->GetFormID();
			}
		}

		RE::NiNode* OnAttach(Attach_t* a_original, void* a_biped, RE::NiNode* a_object, RE::NiNode* a_parent, std::uint32_t a_bipedIndex, bool a_firstPerson)
		{
			Attach info;
			info.bipedIndex = a_bipedIndex;
			info.firstPerson = a_firstPerson;
			// Read what the object carries before the game merges it into the skeleton.
			if (a_object) {
				info.objectName = a_object->name.c_str() ? a_object->name.c_str() : "";
				info.physicsXml = std::string{ hdt::fo4::StringExtraData(a_object, "HDT Skinned Mesh Physics Object"sv) };
			}
			info.model = ModelPath(a_biped, a_bipedIndex);
			ActorOf(a_biped, info.actor, info.formID, info.handle);

			auto* result = a_original(a_biped, a_object, a_parent, a_bipedIndex, a_firstPerson);
			info.attached = RE::NiPointer<RE::NiAVObject>(result);

			std::scoped_lock guard{ g_lock };
			if (info.formID && info.handle && !a_firstPerson && result && (g_seen.size() < 8192 || g_seen.contains(info.formID))) {
				g_seen[info.formID] = info.handle;
			}
			if (info.formID && info.handle && !a_firstPerson && !info.physicsXml.empty() && result) {
				auto& actor = g_actors[info.formID];
				actor.formID = info.formID;
				actor.handle = info.handle;
				++actor.generation;
				g_physicsBySlot[info.formID][info.bipedIndex] = info;
			}
			static std::set<std::pair<std::uint32_t, std::string>> s_seenPhysics;
			// Each actor's first attach of each physics model; with the verbose log, also every player attach and the
			// first 60 others.
			const bool verbose = logger::Verbose();
			const bool interesting = (!info.physicsXml.empty() && s_seenPhysics.insert({ info.formID, info.model }).second) || (verbose && info.formID == 0x14 && !a_firstPerson);
			static int s_others = 0;
			if (g_logged < kMaxLogged && (interesting || (verbose && s_others < 60))) {
				++g_logged;
				if (!interesting) {
					++s_others;
				}
				logger::info("Attach: actor {:08X}{} slot {}{}: '{}' model '{}'{}{} -> {}",
					info.formID, info.actor ? "" : " (unknown)", a_bipedIndex, a_firstPerson ? " (1st person)" : "",
					info.objectName, info.model,
					info.physicsXml.empty() ? "" : ", physics ",
					info.physicsXml,
					result ? std::format("'{}' ({})", result->name.c_str() ? result->name.c_str() : "", hdt::fo4::ClassName(result)) : std::string{ "nothing" });
				if (g_logged == kMaxLogged) {
					logger::info("Attach: {} attaches logged (each actor's first of each physics model, the player's, the first 60 others); not logging more", kMaxLogged);
				}
			}
			g_recent.push_back(std::move(info));
			if (g_recent.size() > kMaxRecent) {
				g_recent.erase(g_recent.begin());
			}
			return result;
		}

		RE::NiNode* Hook1(void* a_biped, RE::NiNode* a_object, RE::NiNode* a_parent, std::uint32_t a_bipedIndex, bool a_firstPerson)
		{
			return OnAttach(g_original1, a_biped, a_object, a_parent, a_bipedIndex, a_firstPerson);
		}

		RE::NiNode* Hook2(void* a_biped, RE::NiNode* a_object, RE::NiNode* a_parent, std::uint32_t a_bipedIndex, bool a_firstPerson)
		{
			return OnAttach(g_original2, a_biped, a_object, a_parent, a_bipedIndex, a_firstPerson);
		}

		// The call at a_offset in ApplySkinnedObjects: true if it is a `call rel32`; logs where it points.
		bool CheckSite(std::uintptr_t a_site, const char* a_label)
		{
			const auto* code = reinterpret_cast<const std::uint8_t*>(a_site);
			const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
			if (code[0] != 0xE8) {
				logger::error("Attach hook: {} (+{:07X}) isn't a call (starts {:02X}); not installed", a_label, a_site - base, code[0]);
				return false;
			}
			std::int32_t rel;
			std::memcpy(&rel, code + 1, sizeof(rel));
			const auto target = a_site + 5 + rel;
			if (target != Offsets::BipedAnimAttachSkinnedObject.address()) {
				logger::warn("Attach hook: {} (+{:07X}) calls +{:X}, not AttachSkinnedObject: another plugin hooked the call; FO4SMP calls it in turn",
					a_label, a_site - base, target - base);
			}
			return true;
		}
	}

	bool Install()
	{
		const REL::Relocation<std::uintptr_t> site1{ Offsets::BipedAnimApplySkinnedObjects, Offsets::BipedAnimApplySkinnedObjects_AttachCall1 };
		const REL::Relocation<std::uintptr_t> site2{ Offsets::BipedAnimApplySkinnedObjects, Offsets::BipedAnimApplySkinnedObjects_AttachCall2 };
		if (!CheckSite(site1.address(), "ApplySkinnedObjects' first attach call") || !CheckSite(site2.address(), "ApplySkinnedObjects' second attach call")) {
			return false;
		}
		auto& trampoline = REL::GetTrampoline();  // allocated by F4SE::Init (main.cpp)
		g_original1 = reinterpret_cast<Attach_t*>(trampoline.write_call<5>(site1.address(), &Hook1));
		g_original2 = reinterpret_cast<Attach_t*>(trampoline.write_call<5>(site2.address(), &Hook2));
		g_installed = true;
		const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
		logger::info("Attach hook: installed at ApplySkinnedObjects' two calls to AttachSkinnedObject (+{:07X}, +{:07X})", site1.address() - base, site2.address() - base);
		return true;
	}

	bool Installed()
	{
		return g_installed;
	}

	std::vector<Attach> PhysicsAttachesOf(std::uint32_t a_formID)
	{
		std::scoped_lock guard{ g_lock };
		std::vector<Attach> out;
		if (const auto it = g_physicsBySlot.find(a_formID); it != g_physicsBySlot.end()) {
			for (const auto& [slot, a] : it->second) {
				out.push_back(a);
			}
		}
		return out;
	}

	std::vector<PhysicsActor> PhysicsActors()
	{
		std::scoped_lock guard{ g_lock };
		std::vector<PhysicsActor> out;
		out.reserve(g_actors.size());
		for (const auto& [id, actor] : g_actors) {
			out.push_back(actor);
		}
		return out;
	}

	std::vector<PhysicsActor> OtherActors()
	{
		std::scoped_lock guard{ g_lock };
		std::vector<PhysicsActor> out;
		for (const auto& [id, handle] : g_seen) {
			if (!g_actors.contains(id)) {
				out.push_back({ id, handle, 0 });
			}
		}
		return out;
	}

	RE::NiAVObject* Loaded3D(std::uint32_t a_handle)
	{
		if (!a_handle) {
			return nullptr;
		}
		static REL::Relocation<Lookup_t*> lookup{ Offsets::LookupREFRByHandle };
		RE::NiPointer<RE::TESObjectREFR> ref;
		if (!lookup(a_handle, ref) || !ref) {
			return nullptr;
		}
		// TESObjectREFR::loadedData (+0xF0), its 3D at +0x08 (F4SE GameReferences.h; CommonLibF4 agrees).
		const auto loaded = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(ref.get()) + 0xF0);
		return loaded ? *reinterpret_cast<RE::NiAVObject* const*>(loaded + 0x08) : nullptr;
	}

	bool FindByAttachedNode(RE::NiAVObject* a_node, Attach& a_out)
	{
		std::scoped_lock guard{ g_lock };
		for (auto it = g_recent.rbegin(); it != g_recent.rend(); ++it) {
			if (it->attached.get() == a_node) {
				a_out = *it;
				return true;
			}
		}
		return false;
	}
}
