#include "PlayerPhysics.h"

#include "Console.h"
#include "Offsets.h"
#include "PhysicsWorld.h"
#include "game/Fo4NiUtils.h"
#include "game/Fo4Skin.h"
#include "game/Fo4System.h"
#include "hdtDefaultBBPList.h"

namespace FO4SMP::PlayerPhysics
{
	namespace
	{
		constexpr auto kPhysicsExtraData = "HDT Skinned Mesh Physics Object"sv;
		constexpr double kScanDelaySeconds = 1.0;    // after the 3D changes, let the outfit attach first
		constexpr double kRescanSeconds = 5.0;       // nothing found: look again this often
		constexpr double kAttachCheckSeconds = 1.0;  // systems built: check they're still attached this often

		struct Built
		{
			RE::BSTSmartPointer<hdt::Fo4System> system;
			RE::NiPointer<RE::NiAVObject> model;  // the node the system was built from
			std::string file;
		};

		RE::NiAVObject* g_root = nullptr;  // only compared, never dereferenced once stale
		std::vector<Built> g_built;
		std::chrono::steady_clock::time_point g_nextCheck{};
		bool g_reportedEmpty = false;

		RE::NiAVObject* PlayerRoot()
		{
			// F4SE's g_player for 1.11.240 (0x032DD370 = Address Library 2698073); CommonLibF4's
			// PlayerCharacter singleton ID (2690919) isn't in Address Library 1.11.240.
			static REL::Relocation<std::uintptr_t*> player{ Offsets::PlayerCharacter_Singleton };
			const auto pc = *player;
			if (!pc) {
				return nullptr;
			}
			// TESObjectREFR::loadedData (+0xF0), its 3D at +0x08 (F4SE GameReferences.h; CommonLibF4 agrees).
			const auto loaded = *reinterpret_cast<const std::uintptr_t*>(pc + 0xF0);
			if (!loaded) {
				return nullptr;
			}
			return *reinterpret_cast<RE::NiAVObject* const*>(loaded + 0x08);
		}

		void Drop(std::string_view a_why)
		{
			if (g_built.empty()) {
				return;
			}
			for (auto& b : g_built) {
				PhysicsWorld::Get()->RemoveSystem(b.system.get());
			}
			logger::info("Player physics: dropped {} system(s) ({})", g_built.size(), a_why);
			g_built.clear();
		}

		bool IsAttachedUnder(RE::NiAVObject* a_object, RE::NiAVObject* a_root)
		{
			for (auto* o = a_object; o; o = o->parent) {
				if (o == a_root) {
					return true;
				}
			}
			return false;
		}

		struct Candidate
		{
			RE::NiAVObject* model;
			std::string file;
			std::string source;
		};

		void Walk(RE::NiAVObject* a_object, const hdt::DefaultBBPList& a_defaults, std::vector<Candidate>& a_out, bool a_log, int a_depth)
		{
			if (!a_object || a_depth > 64) {
				return;
			}

			if (const auto file = hdt::fo4::StringExtraData(a_object, kPhysicsExtraData); !file.empty()) {
				a_out.push_back({ a_object, std::string{ file }, std::format("extra data on {} '{}'", hdt::fo4::ClassName(a_object), a_object->name.c_str()) });
			}

			if (auto* shape = hdt::castBSTriShape(a_object)) {
				hdt::fo4::SkinnedShape skinned;
				const auto error = hdt::fo4::ReadSkinnedShape(shape, skinned, false);
				if (a_log) {
					if (error.empty()) {
						logger::info("Player physics:   shape '{}' ({}): {} bones, {} vertices, {} precision", shape->name.c_str(), hdt::fo4::ClassName(shape),
							skinned.bones.size(), skinned.vertices.size(), skinned.fullPrecision ? "full" : "half");
					} else {
						logger::info("Player physics:   shape '{}' ({}): {}", shape->name.c_str(), hdt::fo4::ClassName(shape), error);
					}
				}
				if (auto it = a_defaults.files.find(shape->name.c_str()); it != a_defaults.files.end() && shape->parent) {
					a_out.push_back({ shape->parent, it->second, std::format("defaultBBPs.xml maps shape '{}'", shape->name.c_str()) });
				}
			}

			if (auto* node = hdt::castNiNode(a_object)) {
				for (auto& child : hdt::fo4::Children(node)) {
					Walk(child.get(), a_defaults, a_out, a_log, a_depth + 1);
				}
			}
		}

		void Scan(RE::NiAVObject* a_root, const hdt::DefaultBBPList& a_defaults)
		{
			auto* skeleton = hdt::castNiNode(a_root);
			if (!skeleton) {
				logger::warn("Player physics: the player's 3D root is a {}, not a node", hdt::fo4::ClassName(a_root));
				return;
			}

			const bool log = !g_reportedEmpty;
			if (log) {
				logger::info("Player physics: scanning the player's 3D '{}' ({})", a_root->name.c_str(), hdt::fo4::ClassName(a_root));
			}
			std::vector<Candidate> candidates;
			Walk(a_root, a_defaults, candidates, log, 0);

			if (candidates.empty()) {
				if (log) {
					logger::info("Player physics: no physics found on the player (no '{}' extra data, no shape named in defaultBBPs.xml); looking again every {} s",
						kPhysicsExtraData, kRescanSeconds);
				}
				g_reportedEmpty = true;
				return;
			}

			// As FSMP: a model's own extra data wins; defaultBBPs.xml only covers shapes when nothing names a file.
			const bool haveExtraData = std::ranges::any_of(candidates, [](const Candidate& c) { return c.source.starts_with("extra data"); });
			std::set<std::pair<RE::NiAVObject*, std::string>> seen;
			for (const auto& c : candidates) {
				if (haveExtraData && !c.source.starts_with("extra data")) {
					logger::info("Player physics: ignoring {} ({}): extra data names the physics", c.file, c.source);
					continue;
				}
				if (!seen.insert({ c.model, c.file }).second) {
					continue;
				}
				logger::info("Player physics: building {} from {} (model node '{}')", c.file, c.source, c.model->name.c_str());

				hdt::DefaultBBP::PhysicsFile_t file{ c.file, {} };
				hdt::Fo4SystemCreator creator;
				const auto start = std::chrono::steady_clock::now();
				auto system = creator.createOrUpdateSystem(skeleton, c.model, &file, {}, nullptr);
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				if (!system) {
					logger::warn("Player physics: {} built nothing (file missing or unreadable, or no bones found; see the lines above)", c.file);
					continue;
				}

				std::size_t dynamic = 0;
				for (auto& bone : system->getBones()) {
					dynamic += bone->m_rig.isStaticOrKinematicObject() ? 0 : 1;
				}
				logger::info("Player physics: {} built in {:.1f} ms: {} bones ({} simulated), {} collision bodies",
					c.file, ms, system->getBones().size(), dynamic, system->meshes().size());
				Console::Print(std::format("FO4SMP: physics on the player from {} ({} bones simulated)", c.file, dynamic));

				PhysicsWorld::Get()->AddSystem(system.get());
				g_built.push_back({ system, RE::NiPointer<RE::NiAVObject>(c.model), c.file });
			}
		}
	}

	void Update(const hdt::DefaultBBPList& a_defaults)
	{
		const auto now = std::chrono::steady_clock::now();
		auto* root = PlayerRoot();

		if (root != g_root) {
			Drop("the player's 3D changed");
			g_root = root;
			g_reportedEmpty = false;
			g_nextCheck = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kScanDelaySeconds));
			return;
		}
		if (!root || now < g_nextCheck) {
			return;
		}

		if (g_built.empty()) {
			Scan(root, a_defaults);
			g_nextCheck = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(g_built.empty() ? kRescanSeconds : kAttachCheckSeconds));
			return;
		}

		for (const auto& b : g_built) {
			if (!IsAttachedUnder(b.model.get(), root)) {
				Drop(std::format("'{}' is no longer attached", b.model->name.c_str()));
				g_reportedEmpty = false;
				g_nextCheck = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kScanDelaySeconds));
				return;
			}
		}
		g_nextCheck = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kAttachCheckSeconds));
	}

	std::string Status()
	{
		if (g_built.empty()) {
			return "no physics on the player yet";
		}
		std::size_t bones = 0, dynamic = 0;
		for (const auto& b : g_built) {
			for (auto& bone : b.system->getBones()) {
				++bones;
				dynamic += bone->m_rig.isStaticOrKinematicObject() ? 0 : 1;
			}
		}
		return std::format("player physics: {} system(s), {} bones ({} simulated)", g_built.size(), bones, dynamic);
	}
}
