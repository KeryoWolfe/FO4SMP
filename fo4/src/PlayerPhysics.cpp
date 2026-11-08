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

		// Diagnostic: where each collision body's vertices and each simulated bone are, in the frame of the
		// skeleton's Root bone (at the feet), computed from the skin data as FSMP's SkinnedMeshBody does.
		// A-Body is about 128 units tall. Also the raw skin-space box of each body and, for the small
		// bodies, each skin bone's bind position (skin space) beside its current position (Root frame).
		RE::NiPoint3 InFrame(const RE::NiTransform& a_frame, const btVector3& a_world)
		{
			RE::NiTransform world;
			world.translate = { a_world.x(), a_world.y(), a_world.z() };
			return hdt::fo4::LocalFromWorld(a_frame, world).translate;
		}

		std::string Box(const btVector3& a_lo, const btVector3& a_hi)
		{
			return std::format("({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f})", a_lo.x(), a_lo.y(), a_lo.z(), a_hi.x(), a_hi.y(), a_hi.z());
		}

		void DumpGeometry(const Built& a_built)
		{
			auto* system = a_built.system.get();
			auto* rootBone = hdt::fo4::findNode(system->m_skeleton.get(), RE::BSFixedString("Root"));
			const auto& frame = rootBone ? rootBone->world : system->m_skeleton->world;
			logger::info("Geometry of {} in the frame of {} (game units):", a_built.file, rootBone ? "the Root bone" : "the skeleton");
			for (auto& body : system->meshes()) {
				if (body->m_vertices.empty()) {
					continue;
				}
				std::vector<hdt::btMatrix4x3T> toWorld;
				for (auto& sb : body->m_skinnedBones) {
					toWorld.push_back(hdt::btMatrix4x3T(sb.ptr->m_currentTransform) * sb.vertexToBone);
				}
				const btVector3 big(1e30f, 1e30f, 1e30f);
				btVector3 sum(0, 0, 0), lo = big, hi = -big, skinLo = big, skinHi = -big;
				for (const auto& v : body->m_vertices) {
					btVector3 p(0, 0, 0);
					for (int k = 0; k < 4; ++k) {
						if (v.m_weight[k] > 0.0f && v.m_boneIdx[k] < toWorld.size()) {
							p += (toWorld[v.m_boneIdx[k]] * v.m_skinPos) * v.m_weight[k];
						}
					}
					const auto r = InFrame(frame, p);
					const btVector3 q(r.x, r.y, r.z);
					sum += q;
					lo.setMin(q);
					hi.setMax(q);
					skinLo.setMin(v.m_skinPos);
					skinHi.setMax(v.m_skinPos);
				}
				sum /= static_cast<float>(body->m_vertices.size());
				logger::info("  body {}: {} vertices, center ({:.1f}, {:.1f}, {:.1f}), box {}; skin-space box {}",
					body->m_name.c_str(), body->m_vertices.size(), sum.x(), sum.y(), sum.z(), Box(lo, hi), Box(skinLo, skinHi));
				if (body->m_vertices.size() <= 400) {
					for (std::size_t i = 0; i < body->m_skinnedBones.size(); ++i) {
						auto& sb = body->m_skinnedBones[i];
						auto* bone = static_cast<hdt::Fo4Bone*>(sb.ptr);
						const auto bind = sb.vertexToBone.toTransform().inverse().getOrigin();
						const auto& w = bone->m_node->world.translate;
						const auto now = InFrame(frame, btVector3(w.x, w.y, w.z));
						const auto& ct = bone->m_currentTransform.getOrigin();
						logger::info("    bone {}: bind position ({:.1f}, {:.1f}, {:.1f}), now ({:.1f}, {:.1f}, {:.1f}), FSMP's copy {:.1f} units from the node",
							bone->m_name.c_str(), bind.x(), bind.y(), bind.z(), now.x, now.y, now.z, (ct - btVector3(w.x, w.y, w.z)).length());
					}
				}
			}
			for (auto& bone : system->getBones()) {
				auto* b = static_cast<hdt::Fo4Bone*>(bone.get());
				const auto& w = b->m_node->world.translate;
				const float off = (b->m_currentTransform.getOrigin() - btVector3(w.x, w.y, w.z)).length();
				if (b->m_rig.isStaticOrKinematicObject() && off > 1.0f) {
					logger::info("  kinematic bone {} (parent '{}'): FSMP's copy {:.1f} units from the node", b->m_name.c_str(),
						b->m_node->parent ? b->m_node->parent->name.c_str() : "none", off);
				}
			}
			for (auto& bone : system->getBones()) {
				auto* b = static_cast<hdt::Fo4Bone*>(bone.get());
				if (b->m_rig.isStaticOrKinematicObject()) {
					continue;
				}
				const auto& w = b->m_node->world.translate;
				const auto node = InFrame(frame, btVector3(w.x, w.y, w.z));
				const auto rig = InFrame(frame, b->m_rig.getWorldTransform().getOrigin());
				logger::info("  simulated bone {} (parent '{}'): node ({:.1f}, {:.1f}, {:.1f}), rigid body ({:.1f}, {:.1f}, {:.1f})",
					b->m_name.c_str(), b->m_node->parent ? b->m_node->parent->name.c_str() : "none", node.x, node.y, node.z, rig.x, rig.y, rig.z);
			}
		}

		// Fallout 4: bones an outfit adds that the skeleton lacks (A-Body's *_SMP bones, Anus_*, Penis_*...)
		// are attached under the skeleton with an identity local transform, so they sit on their parent
		// (run 8: on Root, at the feet) instead of at their place on the body, and the physics is built
		// from that. (FO4 Faster HDT-SMP hooks BSFlattenedBoneTree's bone creation, likely for this.)
		// Until the attach hooks (milestone 4), put each such bone where the skin's bind data says it
		// is, relative to the nearest real bone of the same shape:
		//   world(X) = world(R) * skinToBone(R) * skinToBone(X)^-1
		// and set its local transform from that. A bone is "added" when it hangs off the Root bone (directly
		// or through other added bones) and isn't COM; only real bones serve as anchors.
		void FixAddedBones(RE::NiAVObject* a_model)
		{
			struct Shape
			{
				std::string name;
				hdt::fo4::SkinnedShape skin;
			};
			std::vector<Shape> shapes;
			std::function<void(RE::NiAVObject*, int)> collect = [&](RE::NiAVObject* a_object, int a_depth) {
				if (!a_object || a_depth > 64) {
					return;
				}
				if (auto* tri = hdt::castBSTriShape(a_object)) {
					Shape shape{ tri->name.c_str(), {} };
					if (hdt::fo4::ReadSkinnedShape(tri, shape.skin, false).empty()) {
						shapes.push_back(std::move(shape));
					}
				}
				if (auto* node = hdt::castNiNode(a_object)) {
					for (auto& child : hdt::fo4::Children(node)) {
						collect(child.get(), a_depth + 1);
					}
				}
			};
			collect(a_model, 0);

			// Added bones hang off the skeleton's Root bone (directly, or under another added bone); the
			// only real body bone under Root is COM.
			auto isAdded = [](RE::NiNode* a_node) {
				for (auto* n = a_node; n && n->parent; n = n->parent) {
					if (std::string_view{ n->parent->name.c_str() } == "Root"sv) {
						return std::string_view{ n->name.c_str() } != "COM"sv;
					}
				}
				return false;
			};
			std::unordered_map<RE::NiNode*, int> depth;
			std::vector<RE::NiNode*> added;
			for (const auto& shape : shapes) {
				for (const auto& bone : shape.skin.bones) {
					if (bone.node && isAdded(bone.node) && !depth.contains(bone.node)) {
						int d = 0;
						for (auto* n = bone.node; n; n = n->parent) {
							++d;
						}
						depth[bone.node] = d;
						added.push_back(bone.node);
					}
				}
			}
			std::ranges::sort(added, [&](RE::NiNode* a, RE::NiNode* b) { return depth[a] < depth[b]; });

			int fixed = 0;
			for (auto* node : added) {
				if (!node->parent) {
					continue;
				}
				// The real bone of the same shape whose bind position is nearest.
				const RE::NiTransform* bindX = nullptr;
				const hdt::fo4::SkinBone* best = nullptr;
				const Shape* bestShape = nullptr;
				float bestDistance = 1e30f;
				for (const auto& shape : shapes) {
					const hdt::fo4::SkinBone* self = nullptr;
					for (const auto& b : shape.skin.bones) {
						if (b.node == node) {
							self = &b;
						}
					}
					if (!self) {
						continue;
					}
					const auto selfBind = hdt::fo4::Inverse(self->skinToBone).translate;
					for (const auto& b : shape.skin.bones) {
						if (!b.node || b.node == node || depth.contains(b.node)) {  // anchors are real bones only
							continue;
						}
						const auto bind = hdt::fo4::Inverse(b.skinToBone).translate;
						const float dx = bind.x - selfBind.x, dy = bind.y - selfBind.y, dz = bind.z - selfBind.z;
						const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
						if (distance < bestDistance) {
							bestDistance = distance;
							best = &b;
							bestShape = &shape;
							bindX = &self->skinToBone;
						}
					}
				}
				if (!best) {
					logger::info("Player physics: added bone '{}' has no real bone beside it in any skin; left as it is", node->name.c_str());
					continue;
				}
				const auto world = hdt::fo4::Compose(hdt::fo4::Compose(best->node->world, best->skinToBone), hdt::fo4::Inverse(*bindX));
				const float dx = world.translate.x - node->world.translate.x, dy = world.translate.y - node->world.translate.y, dz = world.translate.z - node->world.translate.z;
				const float moved = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (moved < 0.5f) {
					continue;
				}
				node->local = hdt::fo4::LocalFromWorld(node->parent->world, world);
				node->world = world;
				// Children follow (an added bone's children are added bones too: Anus_00_SMP > Anus_01_Offset >
				// Anus_01); the ones in a skin are placed themselves after this, parents first.
				std::function<void(RE::NiNode*)> follow = [&](RE::NiNode* a_parent) {
					for (auto& child : hdt::fo4::Children(a_parent)) {
						if (auto* c = hdt::castNiNode(child.get())) {
							c->world = hdt::fo4::Compose(a_parent->world, c->local);
							follow(c);
						}
					}
				};
				follow(node);
				++fixed;
				logger::info("Player physics: added bone '{}' (parent '{}') moved {:.1f} units to its bind place beside '{}' (shape '{}')",
					node->name.c_str(), node->parent->name.c_str(), moved, best->node->name.c_str(), bestShape->name);
			}
			logger::info("Player physics: {} added bone(s) placed from the bind data ({} added bones found in the skins)", fixed, added.size());
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
				FixAddedBones(c.model);

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
