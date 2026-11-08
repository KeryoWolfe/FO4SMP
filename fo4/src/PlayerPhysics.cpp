#include "PlayerPhysics.h"

#include "Console.h"
#include "Offsets.h"
#include "PhysicsWorld.h"
#include "game/Fo4NiUtils.h"
#include "game/Fo4Skin.h"
#include "game/Fo4Bone.h"
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

		// Diagnostic: where each collision body's vertices and each simulated bone are, in the skeleton
		// root's frame (x right, y forward, z up, game units). A-Body is about 128 units tall.
		RE::NiPoint3 InRootFrame(const RE::NiTransform& a_root, const btVector3& a_world)
		{
			RE::NiTransform world;
			world.translate = { a_world.x(), a_world.y(), a_world.z() };
			return hdt::fo4::LocalFromWorld(a_root, world).translate;
		}

		void DumpGeometry(const Built& a_built)
		{
			auto* system = a_built.system.get();
			const auto& root = system->m_skeleton->world;
			logger::info("Geometry of {} in the skeleton root's frame (x right, y forward, z up):", a_built.file);
			for (auto& body : system->meshes()) {
				if (body->m_vpos.empty()) {
					continue;
				}
				btVector3 sum(0, 0, 0), lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
				for (const auto& v : body->m_vpos) {
					const auto p = InRootFrame(root, v.pos());
					const btVector3 q(p.x, p.y, p.z);
					sum += q;
					lo.setMin(q);
					hi.setMax(q);
				}
				sum /= static_cast<float>(body->m_vpos.size());
				logger::info("  body {}: {} vertices, center ({:.1f}, {:.1f}, {:.1f}), box ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f})",
					body->m_name.c_str(), body->m_vpos.size(), sum.x(), sum.y(), sum.z(), lo.x(), lo.y(), lo.z(), hi.x(), hi.y(), hi.z());
			}
			for (auto& bone : system->getBones()) {
				auto* b = static_cast<hdt::Fo4Bone*>(bone.get());
				const auto node = InRootFrame(root, btVector3(b->m_node->world.translate.x, b->m_node->world.translate.y, b->m_node->world.translate.z));
				if (b->m_rig.isStaticOrKinematicObject()) {
					continue;
				}
				const auto rig = InRootFrame(root, b->m_rig.getWorldTransform().getOrigin());
				logger::info("  simulated bone {} (parent '{}'): node ({:.1f}, {:.1f}, {:.1f}), rigid body ({:.1f}, {:.1f}, {:.1f})",
					b->m_name.c_str(), b->m_node->parent ? b->m_node->parent->name.c_str() : "none", node.x, node.y, node.z, rig.x, rig.y, rig.z);
			}
		}

		struct Candidate
		{
			RE::NiAVObject* model;
			std::string file;
			std::string source;
		};

		void Walk(RE::NiAVObject* a_root, RE::NiAVObject* a_object, const hdt::DefaultBBPList& a_defaults, std::vector<Candidate>& a_out, bool a_log, int a_depth)
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
					// The outfit's node attached to the skeleton root holds all its shapes (the body and the
					// Virtual* collision shapes); the XML's shapes are looked up under it.
					RE::NiAVObject* model = shape->parent;
					while (model->parent && model->parent != a_root) {
						model = model->parent;
					}
					a_out.push_back({ model, it->second, std::format("defaultBBPs.xml maps shape '{}'", shape->name.c_str()) });
				}
			}

			if (auto* node = hdt::castNiNode(a_object)) {
				for (auto& child : hdt::fo4::Children(node)) {
					Walk(a_root, child.get(), a_defaults, a_out, a_log, a_depth + 1);
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
			Walk(a_root, a_root, a_defaults, candidates, log, 0);

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

		static int dumps = 0;
		static auto nextDump = now;
		if (dumps < 6 && now >= nextDump) {
			for (const auto& b : g_built) {
				DumpGeometry(b);
			}
			++dumps;
			nextDump = now + std::chrono::seconds(dumps < 2 ? 2 : 10);
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
