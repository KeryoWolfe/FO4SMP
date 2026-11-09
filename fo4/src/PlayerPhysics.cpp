#include "PlayerPhysics.h"

#include "AttachHook.h"
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


		// Diagnostic: for each added bone placed from the bind data, its anchor and its pose relative to the
		// anchor at placement, to tell how far the simulation turns it away from its rest pose.
		struct Rest
		{
			RE::NiNode* anchor;
			RE::NiTransform relative;
		};

		// Fallout 4: the added bones hang off Root, so the kinematic ones (Penis_01_Offset > Penis_01,
		// Penis_Balls_01, Anus_00_SMP > Anus_0x...) follow the feet, not the hips: when the pelvis moves in
		// an animation, the simulated bones tied to them are pulled toward a place the body has left
		// (run 13: the penis goes rogue while moving). Each frame, before the physics reads the skeleton,
		// such a subtree is put back where it was relative to the nearest real bone (Penis_Offset,
		// Pelvis_Rear_skin...). A stopgap until the attach hooks give these bones their real parents.
		struct Follow
		{
			RE::NiNode* node;
			RE::NiNode* anchor;
			RE::NiTransform relative;
		};
		// Milestone 4: everything above, per actor (the player and the NPCs being simulated).
		struct ActorState
		{
			std::uint32_t formID = 0;
			std::uint32_t handle = 0;  // the actor's ObjectRefHandle (NPCs), from the attach hook
			bool isPlayer = false;
			bool simulate = true;          // false: only the added bones are kept on their NIF parents (no physics)
			bool placed = false;           // placement-only: done for the current 3D
			std::uint32_t generation = 0;  // the attach hook's count of this actor's physics attaches at the last build

			RE::NiAVObject* root = nullptr;  // only compared, never dereferenced once stale
			std::vector<Built> built;
			std::unordered_map<RE::NiNode*, Rest> rest;
			std::vector<Follow> follow;
			bool usingNifPlacement = false;
			std::vector<RE::NiNode*> restOrder;  // placed added bones, parents first
			RE::NiNode* rootBone = nullptr;
			std::chrono::steady_clock::time_point nextCheck{};
			bool reportedEmpty = false;
			std::chrono::steady_clock::time_point rootSince{};
			std::vector<std::pair<RE::NiAVObject*, std::uint32_t>> unresolvedShapes;  // per shape, filled by Walk
			bool waiting = false;
		};
		std::map<std::uint32_t, ActorState> g_actors;  // by form ID; the player is 0x14
		ActorState* g_current = nullptr;               // the actor the functions below work on

		ActorState& S() { return *g_current; }
		bool Verbose() { return g_current && g_current->isPlayer; }  // per-bone lines for the player only
		std::string Label() { return !g_current || g_current->isPlayer ? std::string{ "Player physics" } : std::format("NPC {:08X} physics", g_current->formID); }

		std::uint32_t g_systemResets = 0;

		// NPCs whose physics keeps exploding first lose their collisions (the chains still hang and jiggle; a corpse's
		// penis then hangs instead of standing out); if they still explode, they are only placed for a while.
		std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> g_noCollision;  // until
		std::unordered_set<std::uint32_t> g_unsimulatable;  // NPCs whose skeleton lacks the body's bones (armor racks): placed only


		// Fallout 4: the skins of shapes the game doesn't draw (A-Body's Virtual* collision shapes) start with
		// null bone entries (run 20: VirtualHands 36 of 38, VirtualArms 18 of 28) and appear to get them
		// later (run 2's second build, after an outfit change, found bones its first build hadn't). Building
		// then leaves the collision surfaces on a few bones. Wait for them, up to kBoneWaitSeconds.
		constexpr double kBoneWaitSeconds = 30.0;

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
			if (S().built.empty()) {
				return;
			}
			for (auto& b : S().built) {
				PhysicsWorld::Get()->RemoveSystem(b.system.get());
			}
			logger::info("{}: dropped {} system(s) ({})", Label(), S().built.size(), a_why);
			S().built.clear();
			S().rest.clear();
			S().restOrder.clear();
			S().follow.clear();
			S().rootBone = nullptr;
		}

		void ClearPlacement()
		{
			S().rest.clear();
			S().restOrder.clear();
			S().follow.clear();
			S().rootBone = nullptr;
			S().placed = false;
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
			auto world = hdt::fo4::Identity();
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
				// From the bone nodes' world transforms (FSMP's m_currentTransform of kinematic bones is left in
				// the step's translation-offset frame).
				for (auto& sb : body->m_skinnedBones) {
					toWorld.push_back(hdt::btMatrix4x3T(hdt::convertNi(static_cast<hdt::Fo4Bone*>(sb.ptr)->m_node->world)) * sb.vertexToBone);
				}
				// Dominant bone per vertex: which bones carry this body's vertices.
				{
					std::unordered_map<std::string, int> dominant;
					for (const auto& v : body->m_vertices) {
						int best = 0;
						for (int k = 1; k < 4; ++k) {
							if (v.m_weight[k] > v.m_weight[best]) {
								best = k;
							}
						}
						if (v.m_boneIdx[best] < body->m_skinnedBones.size()) {
							++dominant[body->m_skinnedBones[v.m_boneIdx[best]].ptr->m_name.c_str()];
						}
					}
					std::vector<std::pair<std::string, int>> top(dominant.begin(), dominant.end());
					std::ranges::sort(top, [](const auto& a, const auto& b) { return a.second > b.second; });
					std::string list;
					for (std::size_t i = 0; i < top.size() && i < 6; ++i) {
						list += std::format("{}{} {}", i ? ", " : "", top[i].first, top[i].second);
					}
					logger::info("  body {}: vertices mostly on {}", body->m_name.c_str(), list);
				}
				const btVector3 big(1e30f, 1e30f, 1e30f);
				btVector3 sum(0, 0, 0), lo = big, hi = -big, skinLo = big, skinHi = -big;
				for (const auto& v : body->m_vertices) {
					btVector3 p(0, 0, 0);
					float weightSum = 0.0f;
					for (int k = 0; k < 4; ++k) {
						if (v.m_weight[k] > 0.0f && v.m_boneIdx[k] < toWorld.size()) {
							p += (toWorld[v.m_boneIdx[k]] * v.m_skinPos) * v.m_weight[k];
							weightSum += v.m_weight[k];
						}
					}
					if (weightSum > 1e-6f) {
						p /= weightSum;  // world coordinates are ~70,000 units from the origin; don't let a short weight sum drag it
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
				if (b->m_rig.isStaticOrKinematicObject()) {
					std::string_view n{ b->m_name.c_str() };
					if (n.starts_with("Penis"sv) || n.starts_with("Anus"sv) || n.find("Butt"sv) != n.npos || n.find("Breast"sv) != n.npos || n.starts_with("Belly"sv) || n.find("Thigh_Low"sv) != n.npos) {
						const auto p = InFrame(frame, btVector3(w.x, w.y, w.z));
						logger::info("  kinematic bone {} (parent '{}'): node ({:.1f}, {:.1f}, {:.1f}), local {}", n, b->m_node->parent ? b->m_node->parent->name.c_str() : "none",
							p.x, p.y, p.z, hdt::fo4::IsIdentity(b->m_node->local) ? "identity" : "set");
					}
				}
			}
			for (auto& bone : system->getBones()) {
				auto* b = static_cast<hdt::Fo4Bone*>(bone.get());
				const auto it = S().rest.find(b->m_node.get());
				if (it == S().rest.end()) {
					continue;
				}
				// How far the bone is turned and moved from its rest pose beside its anchor.
				const auto expected = hdt::fo4::Compose(it->second.anchor->world, it->second.relative);
				const auto diff = hdt::fo4::Mul(b->m_node->world.rotate, hdt::fo4::Transposed(expected.rotate));
				const float trace = diff.entry[0][0] + diff.entry[1][1] + diff.entry[2][2];
				const float angle = std::acos(std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f)) * 57.29578f;
				const auto& w = b->m_node->world.translate;
				const auto& e = expected.translate;
				const float dist = std::sqrt((w.x - e.x) * (w.x - e.x) + (w.y - e.y) * (w.y - e.y) + (w.z - e.z) * (w.z - e.z));
				logger::info("  {} bone {}: {:.1f} degrees and {:.1f} units from its rest pose beside '{}'", b->m_rig.isStaticOrKinematicObject() ? "kinematic" : "simulated",
					b->m_name.c_str(), angle, dist, it->second.anchor->name.c_str());
			}
			for (auto& bone : system->getBones()) {
				auto* b = static_cast<hdt::Fo4Bone*>(bone.get());
				if (b->m_rig.isStaticOrKinematicObject()) {
					continue;
				}
				const auto& w = b->m_node->world.translate;
				const auto node = InFrame(frame, btVector3(w.x, w.y, w.z));
				const auto rig = InFrame(frame, b->m_rig.getWorldTransform().getOrigin());
				const auto inertia = b->m_rig.getInvInertiaDiagLocal();
				logger::info("  simulated bone {} (parent '{}'): node ({:.1f}, {:.1f}, {:.1f}), rigid body ({:.1f}, {:.1f}, {:.1f}); mass {:.3f}, inverse inertia ({:.2f}, {:.2f}, {:.2f}), scale node {:.3f} / physics {:.3f}, speed {:.1f}",
					b->m_name.c_str(), b->m_node->parent ? b->m_node->parent->name.c_str() : "none", node.x, node.y, node.z, rig.x, rig.y, rig.z,
					b->m_rig.getInvMass() > 0 ? 1.0f / b->m_rig.getInvMass() : 0.0f, inertia.x(), inertia.y(), inertia.z(), b->m_node->world.scale,
					b->m_currentTransform.getScale(), b->m_rig.getLinearVelocity().length());
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
		// or through other added bones) outside COM_Offset / COM; only real bones serve as anchors.
		void FixAddedBones(RE::NiNode* a_skeleton, RE::NiAVObject* a_model)
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
			// real body hangs off Root through COM_Offset > COM.
			auto isAdded = [](RE::NiNode* a_node) {
				for (auto* n = a_node; n && n->parent; n = n->parent) {
					if (std::string_view{ n->parent->name.c_str() } == "Root"sv) {
						return !std::string_view{ n->name.c_str() }.starts_with("COM"sv);  // Root > COM_Offset > COM
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
				// The anchor: the real bone the flesh around this bone follows, i.e. the real bone with the most
				// skin weight on the vertices this bone also weighs (summed over the shapes it skins). The
				// nearest bind position (the first try, run 11) picked Chest for the genitals and buttocks, which
				// twists them with the torso. Nearest bind position remains the fallback.
				const RE::NiTransform* bindX = nullptr;
				const hdt::fo4::SkinBone* best = nullptr;
				const Shape* bestShape = nullptr;
				float bestScore = 0.0f;
				for (const auto& shape : shapes) {
					int selfIndex = -1;
					for (std::size_t i = 0; i < shape.skin.bones.size(); ++i) {
						if (shape.skin.bones[i].node == node) {
							selfIndex = static_cast<int>(i);
						}
					}
					if (selfIndex < 0) {
						continue;
					}
					std::vector<float> score(shape.skin.bones.size(), 0.0f);
					for (const auto& v : shape.skin.vertices) {
						float self = 0.0f;
						for (int k = 0; k < 4; ++k) {
							if (v.bones[k] == selfIndex) {
								self += v.weights[k];
							}
						}
						if (self <= 0.0f) {
							continue;
						}
						for (int k = 0; k < 4; ++k) {
							const auto& b = shape.skin.bones[v.bones[k]];
							if (v.weights[k] > 0.0f && b.node && b.node != node && !depth.contains(b.node)) {
								score[v.bones[k]] += self * v.weights[k];
							}
						}
					}
					{
						std::vector<std::size_t> order(score.size());
						std::iota(order.begin(), order.end(), std::size_t{ 0 });
						std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return score[a] > score[b]; });
						std::string top;
						for (std::size_t r = 0; r < order.size() && r < 3 && score[order[r]] > 0.0f; ++r) {
							top += std::format("{}'{}' {:.1f}", r ? ", " : "", shape.skin.bones[order[r]].node->name.c_str(), score[order[r]]);
						}
						if (!top.empty()) {
							if (Verbose()) logger::info("{}:   '{}' in '{}': shared skin weight with {}", Label(), node->name.c_str(), shape.name, top);
						}
					}
					for (std::size_t i = 0; i < score.size(); ++i) {
						if (score[i] > bestScore) {
							bestScore = score[i];
							best = &shape.skin.bones[i];
							bestShape = &shape;
							bindX = &shape.skin.bones[selfIndex].skinToBone;
						}
					}
				}
				if (!best) {  // fallback: the real bone of the same shape whose bind position is nearest
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
							if (!b.node || b.node == node || depth.contains(b.node)) {
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
				}
				if (!best) {
					if (Verbose()) logger::info("{}: added bone '{}' has no real bone beside it in any skin; left as it is", Label(), node->name.c_str());
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
				S().rest[node] = { best->node, hdt::fo4::LocalFromWorld(best->node->world, world) };
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
				if (Verbose()) logger::info("{}: added bone '{}' (parent '{}') moved {:.1f} units to its bind place beside '{}' (shape '{}')", Label(),
					node->name.c_str(), node->parent->name.c_str(), moved, best->node->name.c_str(), bestShape->name);
			}
			if (Verbose()) logger::info("{}: {} added bone(s) placed from the bind data ({} added bones found in the skins)", Label(), fixed, added.size());

			// (Restored in run 23: without this, run 22 had 2,600-2,900 rejected poses per 10 s against 8-123.)
			// The kinematic partners of the simulated bones (Penis_01 for Penis_01_SMP, Penis_Offset for
			// Penis_Offset_SMP...) carry no vertices, so they have no bind data. If one still has an identity
			// local transform, put it where its _SMP bone now is: FSMP's XMLs pair each simulated bone with a
			// kinematic one at the same rest place.
			for (auto* smp : added) {
				std::string_view name{ smp->name.c_str() };
				if (!name.ends_with("_SMP"sv)) {
					continue;
				}
				const RE::BSFixedString partnerName{ std::string{ name.substr(0, name.size() - 4) }.c_str() };
				auto* partner = hdt::fo4::findNode(a_skeleton, partnerName);
				if (!partner || !partner->parent || depth.contains(partner) || !hdt::fo4::IsIdentity(partner->local)) {
					continue;
				}
				const float dx = smp->world.translate.x - partner->world.translate.x, dy = smp->world.translate.y - partner->world.translate.y,
							dz = smp->world.translate.z - partner->world.translate.z;
				const float moved = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (moved < 0.5f) {
					continue;
				}
				RE::NiTransform world = partner->world;
				world.translate = smp->world.translate;
				partner->local = hdt::fo4::LocalFromWorld(partner->parent->world, world);
				partner->world = world;
				// Its children (Anus_01 is Anus_01_SMP's parent) keep their place.
				for (auto& child : hdt::fo4::Children(partner)) {
					if (child) {
						child->local = hdt::fo4::LocalFromWorld(partner->world, child->world);
					}
				}
				if (Verbose()) logger::info("{}: kinematic partner '{}' (parent '{}') moved {:.1f} units to '{}'", Label(), partner->name.c_str(), partner->parent->name.c_str(), moved, name);
			}
		}

		// Logs each collision body's tags and filters, and which body pairs the filters let collide, so a
		// contact between bodies the XML keeps apart (e.g. VirtualPenis vs VirtualLegs) shows up as a filter bug.
		void LogCollisionFilter(hdt::Fo4System* a_system)
		{
			const auto& meshes = a_system->meshes();
			auto join = [](const auto& a_set) {
				std::string out;
				for (const auto& t : a_set) {
					out += (out.empty() ? "" : ",") + std::format("{}@{:X}", t.c_str(), reinterpret_cast<std::uintptr_t>(t.data()));
				}
				return out;
			};
			for (auto& m : meshes) {
				logger::info("Collision body {}: kinematic {}, {} bones, tags [{}], no-collide-with [{}], can-collide-with [{}], {} no-collide bones",
					m->m_name.c_str(), m->m_isKinematic, m->m_skinnedBones.size(), join(m->m_tags), join(m->m_noCollideWithTags),
					join(m->m_canCollideWithTags), m->m_noCollideWithBones.size());
			}
			std::string pairs;
			int count = 0;
			for (std::size_t i = 0; i < meshes.size(); ++i) {
				for (std::size_t j = i + 1; j < meshes.size(); ++j) {
					auto* a = meshes[i].get();
					auto* b = meshes[j].get();
					if (a->m_isKinematic && b->m_isKinematic) {
						continue;
					}
					if (a->canCollideWith(b) && b->canCollideWith(a)) {
						pairs += std::format(" {}/{};", a->m_name.c_str(), b->m_name.c_str());
						++count;
					}
				}
			}
			logger::info("Collision filter: {} body pair(s) may collide:{}", count, pairs);
		}

		void BuildFollow(RE::NiNode* a_skeleton, hdt::Fo4System* a_system)
		{
			auto* rootBone = hdt::fo4::findNode(a_skeleton, RE::BSFixedString("Root"));
			if (!rootBone) {
				return;
			}
			std::unordered_set<RE::NiAVObject*> kinematic, simulated;
			for (auto& bone : a_system->getBones()) {
				auto* b = static_cast<hdt::Fo4Bone*>(bone.get());
				(b->m_rig.isStaticOrKinematicObject() ? kinematic : simulated).insert(b->m_node.get());
			}
			// Real bones (the COM_Offset subtree) as anchor candidates.
			std::vector<RE::NiNode*> real;
			std::function<void(RE::NiNode*, int)> collectReal = [&](RE::NiNode* a_node, int a_depth) {
				if (a_depth > 64) {
					return;
				}
				real.push_back(a_node);
				for (auto& child : hdt::fo4::Children(a_node)) {
					if (auto* c = hdt::castNiNode(child.get())) {
						collectReal(c, a_depth + 1);
					}
				}
			};
			std::function<bool(RE::NiNode*, int)> holdsKinematic = [&](RE::NiNode* a_node, int a_depth) {
				if (a_depth > 64) {
					return false;
				}
				if (kinematic.contains(a_node)) {
					return true;
				}
				for (auto& child : hdt::fo4::Children(a_node)) {
					if (auto* c = hdt::castNiNode(child.get()); c && holdsKinematic(c, a_depth + 1)) {
						return true;
					}
				}
				return false;
			};
			if (S().usingNifPlacement) {
				// Every kinematic added bone follows its NIF parent with its NIF local transform.
				for (auto* node : S().restOrder) {
					if (simulated.contains(node)) {
						continue;
					}
					const auto& rest = S().rest.at(node);
					S().follow.push_back({ node, rest.anchor, rest.relative });
				}
				logger::info("{}: {} kinematic added bone(s) follow their NIF parents each frame", Label(), S().follow.size());
				S().rootBone = rootBone;
				return;
			}
			std::vector<RE::NiNode*> roots;
			for (auto& child : hdt::fo4::Children(rootBone)) {
				auto* c = hdt::castNiNode(child.get());
				if (!c) {
					continue;
				}
				if (std::string_view{ c->name.c_str() }.starts_with("COM"sv)) {
					collectReal(c, 0);
				} else if (!simulated.contains(c) && holdsKinematic(c, 0)) {
					roots.push_back(c);
				}
			}
			for (auto* node : roots) {
				RE::NiNode* anchor = nullptr;
				float best = 1e30f;
				for (auto* r : real) {
					const auto& a = r->world.translate;
					const auto& b = node->world.translate;
					const float d = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
					if (d < best && std::string_view{ r->name.c_str() } != "COM_Offset"sv && std::string_view{ r->name.c_str() } != "COM"sv) {
						best = d;
						anchor = r;
					}
				}
				if (!anchor) {
					continue;
				}
				S().follow.push_back({ node, anchor, hdt::fo4::LocalFromWorld(anchor->world, node->world) });
				if (Verbose()) logger::info("{}: kinematic added bone '{}' (under Root) now follows '{}' ({:.1f} units away)", Label(), node->name.c_str(), anchor->name.c_str(), std::sqrt(best));
			}
			S().rootBone = rootBone;
		}

		void ApplyFollow()
		{
			if (!S().rootBone) {
				return;
			}
			std::function<void(RE::NiNode*, int)> follow = [&](RE::NiNode* a_parent, int a_depth) {
				if (a_depth > 64) {
					return;
				}
				for (auto& child : hdt::fo4::Children(a_parent)) {
					if (auto* c = hdt::castNiNode(child.get())) {
						c->world = hdt::fo4::Compose(a_parent->world, c->local);
						follow(c, a_depth + 1);
					}
				}
			};
			for (const auto& f : S().follow) {
				const auto world = hdt::fo4::Compose(f.anchor->world, f.relative);
				f.node->local = hdt::fo4::LocalFromWorld(f.node->parent ? f.node->parent->world : S().rootBone->world, world);
				f.node->world = world;
				follow(f.node, 0);
			}
		}

		// The NIF the physics' shapes come from, for the skin bone names the game leaves null (see
		// Fo4SystemCreator::m_nifBoneNames). Until the attach hook knows the outfit's model path (milestone 4),
		// it is found from the physics XML's name: <xml path>.nif, then
		// Meshes\Actors\Character\CharacterAssets\<xml name>.nif (A-Body's MaleBody.xml / MaleBody.nif), and
		// is used only if it has a bone list for every shape it is asked about.
		struct NifInfo
		{
			std::unordered_map<std::string, std::vector<std::string>> boneNames;  // shape -> skin bone names
			std::unordered_map<std::string, hdt::fo4::NifNode> nodes;            // node -> parent, local transform
		};
		std::unordered_map<std::string, NifInfo> g_nifCache;

		const NifInfo* FindNif(const std::string& a_xml, const std::vector<std::string>& a_shapes, const std::string& a_nif = {})
		{
			std::filesystem::path xml{ a_xml };
			std::vector<std::string> paths;
			if (!a_nif.empty()) {
				paths.push_back(a_nif);  // the attached model's own NIF (milestone 4), tried first
			}
			auto withNif = xml;
			withNif.replace_extension(".nif");
			paths.push_back(withNif.string());
			paths.push_back((std::filesystem::path{ "Meshes" } / "Actors" / "Character" / "CharacterAssets" / xml.stem()).string() + ".nif");
			for (const auto& path : paths) {
				auto it = g_nifCache.find(path);
				if (it == g_nifCache.end()) {
					NifInfo info;
					const auto data = hdt::fo4::readAllFile(path.c_str());
					auto error = data.empty() ? std::string{ "not found (loose files only)" } : hdt::fo4::ReadNifSkinBoneNames(data, info.boneNames);
					if (error.empty()) {
						if (auto nodeError = hdt::fo4::ReadNifNodes(data, info.nodes); !nodeError.empty()) {
							error = "node tree: " + nodeError;
						}
					}
					logger::info("{}: NIF {}: {}", Label(), path, error.empty() ? std::format("{} skinned shape(s), {} node(s)", info.boneNames.size(), info.nodes.size()) : error);
					it = g_nifCache.emplace(path, std::move(info)).first;
				}
				if (!it->second.boneNames.empty() && std::ranges::all_of(a_shapes, [&](const std::string& s) { return it->second.boneNames.contains(s); })) {
					logger::info("{}: using {} for bone names and the added bones' places", Label(), path);
					return &it->second;
				}
			}
			return nullptr;
		}

		// Fallout 4: place the bones an outfit adds (they hang off Root with an identity local transform) where
		// its NIF puts them: under their NIF parent with their NIF local transform. In A-Body's NIF every *_SMP
		// bone is a child of its kinematic partner with the identity (Penis_01_SMP under Penis_01,
		// Breast_skin_SMP.L under LBreast_skin), so its rest place is its partner's place. Replaces the bind-data
		// placement (FixAddedBones), which anchored on whichever real bone shared the most skin weight.
		void PlaceAddedBonesFromNif(RE::NiNode* a_skeleton, const NifInfo& a_nif)
		{
			auto* rootBone = hdt::fo4::findNode(a_skeleton, RE::BSFixedString("Root"));
			if (!rootBone) {
				return;
			}
			std::vector<RE::NiNode*> added;
			std::function<void(RE::NiNode*, int)> collect = [&](RE::NiNode* a_node, int a_depth) {
				if (a_depth > 64) {
					return;
				}
				added.push_back(a_node);
				for (auto& child : hdt::fo4::Children(a_node)) {
					if (auto* c = hdt::castNiNode(child.get())) {
						collect(c, a_depth + 1);
					}
				}
			};
			for (auto& child : hdt::fo4::Children(rootBone)) {
				if (auto* c = hdt::castNiNode(child.get()); c && !std::string_view{ c->name.c_str() }.starts_with("COM"sv)) {
					collect(c, 0);
				}
			}
			std::erase_if(added, [&](RE::NiNode* n) { return !a_nif.nodes.contains(n->name.c_str()); });
			std::ranges::sort(added, [&](RE::NiNode* a, RE::NiNode* b) { return a_nif.nodes.at(a->name.c_str()).depth < a_nif.nodes.at(b->name.c_str()).depth; });

			int placed = 0;
			for (auto* node : added) {
				const auto& nif = a_nif.nodes.at(node->name.c_str());
				auto* parent = nif.parent.empty() ? nullptr : hdt::fo4::findNode(a_skeleton, RE::BSFixedString(nif.parent.c_str()));
				if (!parent || !node->parent || parent == node) {
					if (Verbose()) logger::info("{}: added bone '{}': NIF parent '{}' isn't in the skeleton; left as it is", Label(), node->name.c_str(), nif.parent);
					continue;
				}
				const auto world = hdt::fo4::Compose(parent->world, nif.local);
				const float dx = world.translate.x - node->world.translate.x, dy = world.translate.y - node->world.translate.y, dz = world.translate.z - node->world.translate.z;
				node->local = hdt::fo4::LocalFromWorld(node->parent->world, world);
				node->world = world;
				std::function<void(RE::NiNode*, int)> follow = [&](RE::NiNode* a_parent, int a_depth) {
					if (a_depth > 64) {
						return;
					}
					for (auto& child : hdt::fo4::Children(a_parent)) {
						if (auto* c = hdt::castNiNode(child.get())) {
							c->world = hdt::fo4::Compose(a_parent->world, c->local);
							follow(c, a_depth + 1);
						}
					}
				};
				follow(node, 0);
				S().rest[node] = { parent, nif.local };
				S().restOrder.push_back(node);
				++placed;
				if (Verbose()) logger::info("{}: added bone '{}' placed under '{}' as in the NIF (moved {:.1f} units)", Label(), node->name.c_str(), nif.parent, std::sqrt(dx * dx + dy * dy + dz * dz));
			}
			logger::info("{}: {} added bone(s) placed from the NIF", Label(), placed);
		}

		struct Candidate
		{
			RE::NiAVObject* model;
			std::string file;
			std::string source;
			std::string nif;  // the model's NIF when known (from the attach), else ""
		};

		// Names of the skinned shapes under a_object.
		std::vector<std::string> SkinnedShapeNames(RE::NiAVObject* a_object)
		{
			std::vector<std::string> names;
			std::function<void(RE::NiAVObject*, int)> walk = [&](RE::NiAVObject* a_o, int a_depth) {
				if (!a_o || a_depth > 64) {
					return;
				}
				if (auto* tri = hdt::castBSTriShape(a_o)) {
					hdt::fo4::SkinnedShape probe;
					if (hdt::fo4::ReadSkinnedShape(tri, probe, false).empty()) {
						names.emplace_back(tri->name.c_str());
					}
				}
				if (auto* n = hdt::castNiNode(a_o)) {
					for (auto& child : hdt::fo4::Children(n)) {
						walk(child.get(), a_depth + 1);
					}
				}
			};
			walk(a_object, 0);
			return names;
		}

		// Milestone 4: the player's physics from the attach hook: each third-person attach on the player that
		// named a physics XML and is still under the player's 3D. One per XML: an A-Body-refitted armor piece
		// names the body's XML too, so the attach with the most skinned shapes (the body, with its Virtual*
		// collision shapes) wins.
		std::vector<Candidate> AttachCandidates(RE::NiAVObject* a_root)
		{
			std::map<std::string, std::pair<Candidate, std::size_t>> best;
			for (const auto& a : AttachHook::PhysicsAttachesOf(S().formID)) {
				if (!IsAttachedUnder(a.attached.get(), a_root)) {
					continue;
				}
				const auto shapes = SkinnedShapeNames(a.attached.get()).size();
				std::string key = a.physicsXml;
				std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				Candidate c{ a.attached.get(), a.physicsXml, std::format("the attach of '{}' (slot {}, {} skinned shapes)", a.model, a.bipedIndex, shapes),
					a.model.empty() ? std::string{} : (std::filesystem::path{ "Meshes" } / a.model).string() };
				if (auto it = best.find(key); it == best.end() || shapes >= it->second.second) {
					best[key] = { std::move(c), shapes };
				}
			}
			std::vector<Candidate> out;
			for (auto& [key, entry] : best) {
				if (entry.second > 0) {
					out.push_back(std::move(entry.first));
				}
			}
			return out;
		}

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
				if (error.empty() && skinned.unresolved) {
					S().unresolvedShapes.push_back({ shape, skinned.unresolved });
				}
				if (a_log) {
					if (error.empty()) {
						if (Verbose()) logger::info("{}:   shape '{}' ({}): {} bones, {} vertices, {} precision", Label(), shape->name.c_str(), hdt::fo4::ClassName(shape),
							skinned.bones.size(), skinned.vertices.size(), skinned.fullPrecision ? "full" : "half");
					} else {
						if (Verbose()) logger::info("{}:   shape '{}' ({}): {}", Label(), shape->name.c_str(), hdt::fo4::ClassName(shape), error);
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
				logger::warn("{}: the player's 3D root is a {}, not a node", Label(), hdt::fo4::ClassName(a_root));
				return;
			}

			const bool log = !S().reportedEmpty;
			if (log) {
				logger::info("{}: scanning the 3D '{}' ({})", Label(), a_root->name.c_str(), hdt::fo4::ClassName(a_root));
			}
			std::vector<Candidate> candidates;
			S().unresolvedShapes.clear();
			S().waiting = false;
			Walk(a_root, a_root, a_defaults, candidates, log, 0);
			// Milestone 4: what the attach hook saw wins over the scan (extra data is gone after the attach, and
			// defaultBBPs.xml was a stand-in); the scan stays as the fallback.
			if (auto fromAttaches = AttachCandidates(a_root); !fromAttaches.empty()) {
				candidates = std::move(fromAttaches);
			}
			// Only the shapes the physics will be built from count (the head's or a weapon's don't).
			std::uint32_t g_unresolved = 0;
			for (const auto& [shape, count] : S().unresolvedShapes) {
				if (std::ranges::any_of(candidates, [&](const Candidate& c) { return IsAttachedUnder(shape, c.model); })) {
					g_unresolved += count;
				}
			}

			const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - S().rootSince).count();
			if (!candidates.empty() && g_unresolved > 0) {
				if (false && waited < kBoneWaitSeconds) {  // the NIF's bone names fill them now (kept for reference)
					logger::info("{}: {} skin bone(s) of the player's shapes aren't set yet ({:.0f} s after the 3D loaded); waiting", Label(), g_unresolved, waited);
					S().waiting = true;
					S().reportedEmpty = true;  // don't list the shapes again
					return;
				}
				logger::info("{}: {} skin bone(s) of the physics' shapes are unset ({:.0f} s after the 3D loaded); filling them from the NIF", Label(), g_unresolved, waited);
			} else if (!candidates.empty()) {
				logger::info("{}: all skin bones set ({:.0f} s after the 3D loaded)", Label(), waited);
			}

			if (candidates.empty()) {
				if (log) {
					logger::info("{}: no physics found (no '{}' extra data, no shape named in defaultBBPs.xml); looking again every {} s", Label(),
						kPhysicsExtraData, kRescanSeconds);
				}
				S().reportedEmpty = true;
				return;
			}

			// As FSMP: a model's own extra data wins; defaultBBPs.xml only covers shapes when nothing names a file.
			const bool haveExtraData = std::ranges::any_of(candidates, [](const Candidate& c) { return c.source.starts_with("extra data"); }) &&
			                           !std::ranges::any_of(candidates, [](const Candidate& c) { return c.source.starts_with("the attach"); });
			std::set<std::pair<RE::NiAVObject*, std::string>> seen;
			for (const auto& c : candidates) {
				if (haveExtraData && !c.source.starts_with("extra data")) {
					logger::info("{}: ignoring {} ({}): extra data names the physics", Label(), c.file, c.source);
					continue;
				}
				if (!seen.insert({ c.model, c.file }).second) {
					continue;
				}
				logger::info("{}: building {} from {} (model node '{}')", Label(), c.file, c.source, c.model->name.c_str());
				// The outfit's NIF: skin bone names for the null entries, and the added bones' places.
				const std::vector<std::string> nifShapes = SkinnedShapeNames(c.model);
				const NifInfo* nif = FindNif(c.file, nifShapes, c.nif);
				S().usingNifPlacement = nif && !nif->nodes.empty();
				if (S().usingNifPlacement) {
					PlaceAddedBonesFromNif(skeleton, *nif);
				} else {
					logger::warn("{}: no NIF found for {} with all {} skinned shape(s); placing added bones from the bind data and leaving null skin bones unset", Label(), c.file, nifShapes.size());
					FixAddedBones(skeleton, c.model);
				}

				hdt::DefaultBBP::PhysicsFile_t file{ c.file, {} };
				hdt::Fo4SystemCreator creator;
				creator.m_nifBoneNames = nif ? &nif->boneNames : nullptr;
				const auto start = std::chrono::steady_clock::now();
				auto system = creator.createOrUpdateSystem(skeleton, c.model, &file, {}, nullptr);
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				if (!system) {
					logger::warn("{}: {} built nothing (file missing or unreadable, or no bones found; see the lines above)", Label(), c.file);
					continue;
				}

				// An NPC whose skeleton lacks the body's skin bones (DLC05 armor racks: their skeleton has none of the
				// *_skin bones A-Body and TWB hang their SMP bones from) can't be simulated: its chains have nothing to
				// hang from and explode even without collisions (run 35). Keep it placed instead.
				if (!S().isPlayer && creator.m_unknownSkinBones > 0) {
					logger::info("{}: {} skin bone(s) of {}'s shapes aren't in this skeleton (an armor rack?); not simulated, placed only", Label(),
						creator.m_unknownSkinBones, c.file);
					g_unsimulatable.insert(S().formID);
					S().simulate = false;
					S().root = nullptr;
					ClearPlacement();
					return;
				}

				std::size_t dynamic = 0;
				for (auto& bone : system->getBones()) {
					dynamic += bone->m_rig.isStaticOrKinematicObject() ? 0 : 1;
				}
				logger::info("{}: {} built in {:.1f} ms: {} bones ({} simulated), {} collision bodies", Label(),
					c.file, ms, system->getBones().size(), dynamic, system->meshes().size());
				if (Verbose()) {
					Console::Print(std::format("FO4SMP: physics on the player from {} ({} bones simulated)", c.file, dynamic));
				}

				// A body's ground plane (A-Body's and TWB's VirtualGround: a square on the Root bone at the feet, shared
				// "public") collides with every other actor too, so an NPC standing a step higher or lower puts an invisible
				// floor through the player's groin or a woman's chest (run 36: the player's penis and Cait's breasts touching
				// "Root(k)" thousands of times while near settlers). Keep ground planes to their own skeleton.
				for (auto& mesh : system->meshes()) {
					auto* body = static_cast<hdt::Fo4Body*>(mesh.get());
					auto lower = [](std::string a_s) {
						std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						return a_s;
					};
					bool ground = lower(body->m_name.c_str()).find("ground") != std::string::npos;
					for (const auto& tag : body->m_tags) {
						ground |= lower(tag.c_str()).find("ground") != std::string::npos;
					}
					if (ground && body->m_shared == hdt::Fo4Body::SharedType::SHARED_PUBLIC) {
						body->m_shared = hdt::Fo4Body::SharedType::SHARED_INTERNAL;
						if (Verbose()) logger::info("{}: collision body {} is a ground plane; it collides with this actor only", Label(), body->m_name.c_str());
					}
				}
				if (!S().isPlayer && g_noCollision.contains(S().formID)) {
					for (auto& mesh : system->meshes()) {
						static_cast<hdt::Fo4Body*>(mesh.get())->m_disabled = true;
					}
					logger::info("{}: collisions off for this NPC (its physics kept exploding)", Label());
				}
				LogCollisionFilter(system.get());
				BuildFollow(skeleton, system.get());
				PhysicsWorld::Get()->AddSystem(system.get());
				S().built.push_back({ system, RE::NiPointer<RE::NiAVObject>(c.model), c.file });
			}
		}
	}

	namespace
	{
		// Placement only (NPCs not simulated): the outfit's added bones hang off Root with an identity local, so a
		// body with SMP bones collapses (pecs and breasts sink, run 32-33) until something places them. Put each on its
		// NIF parent with its NIF local and keep it there every frame: the body's rest shape, without physics.
		bool PlaceOnly(RE::NiAVObject* a_root)
		{
			auto* skeleton = hdt::castNiNode(a_root);
			if (!skeleton) {
				return false;
			}
			for (const auto& c : AttachCandidates(a_root)) {
				const NifInfo* nif = FindNif(c.file, SkinnedShapeNames(c.model), c.nif);
				if (!nif || nif->nodes.empty()) {
					continue;
				}
				PlaceAddedBonesFromNif(skeleton, *nif);
			}
			if (S().restOrder.empty()) {
				return false;
			}
			for (auto* node : S().restOrder) {
				const auto& rest = S().rest.at(node);
				S().follow.push_back({ node, rest.anchor, rest.relative });
			}
			S().rootBone = hdt::fo4::findNode(skeleton, RE::BSFixedString("Root"));
			S().placed = S().rootBone != nullptr;
			return S().placed;
		}

		int g_maxActive = 5;                       // smp.maximumActiveSkeletons: the player and up to this many minus one NPCs
		constexpr float kNpcMaxDistance = 2000.0f;  // game units (about 28 m); NPCs farther away aren't simulated
		constexpr double kSelectSeconds = 1.0;      // how often the simulated NPCs are chosen again
		constexpr float kNpcKeepBonus = 500.0f;     // selection hysteresis, game units
		constexpr float kPlaceDistance = 8000.0f;   // NPCs not simulated but this near get their added bones placed

		RE::NiAVObject* RootOf(const ActorState& a_state)
		{
			return a_state.isPlayer ? PlayerRoot() : AttachHook::Loaded3D(a_state.handle);
		}

		// NPCs whose physics keeps exploding (a corpse in a ragdoll pose, a body the XML doesn't suit) are dropped
		// for a while: more than kQuarantineResets resets within kQuarantineWindow seconds -> kQuarantineSeconds off.
		constexpr int    kQuarantineResets = 3;
		constexpr double kQuarantineWindow = 10.0;
		constexpr double kQuarantineSeconds = 60.0;
		std::unordered_map<std::uint32_t, std::vector<std::chrono::steady_clock::time_point>> g_resetTimes;
		std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> g_quarantined;  // until

		// After an explosion: the simulated bones of the actor(s) whose bones exploded back on their rest pose, at
		// rest. Only those actors: resetting everyone made the player's penis snap straight and shake whenever a
		// nearby NPC's exploded (run 32).
		void ResetAfterExplosion(std::chrono::steady_clock::time_point a_now)
		{
			++g_systemResets;
			std::vector<std::uint32_t> quarantine;
			for (auto& [id, state] : g_actors) {
				std::set<std::string> explodedBones;
				for (const auto& b : state.built) {
					for (auto& bone : b.system->getBones()) {
						auto* fb = static_cast<hdt::Fo4Bone*>(bone.get());
						if (fb->m_exploded) {
							explodedBones.insert(fb->m_name.c_str());
						}
						fb->m_exploded = false;
					}
				}
				if (explodedBones.empty()) {
					continue;
				}
				g_current = &state;
				// Diagnostic (run 34): what those bones were touching in the step that blew up, with the solver's impulses.
				static int s_explosionsLogged = 0;
				if (s_explosionsLogged < 40) {
					++s_explosionsLogged;
					std::string names;
					for (const auto& n : explodedBones) {
						names += (names.empty() ? "" : ", ") + n;
					}
					logger::info("{}: explosion of {}; their contacts in that step:{}", Label(), names, PhysicsWorld::Get()->StepContactsInvolving(explodedBones));
				}
				// Bullet disables ("breaks") a constraint whose impulse reaches its breaking threshold (infinite by
				// default); an exploded step gives infinite impulses, so the joints would stay off for good and the
				// bones fall freely (run 25). Turn every constraint between a simulated bone and anything back on.
				int reenabled = 0;
				for (const auto& b : S().built) {
					reenabled += b.system->ReenableConstraints();
				}
				if (reenabled) {
					static int s_logged = 0;
					if (s_logged < 20) {
						++s_logged;
						logger::warn("{}: {} constraint(s) had been disabled (broken) by the explosion; turned back on", Label(), reenabled);
					}
				}
				// A rejected (exploded) pose leaves its neighbours strained; resetting one bone onto its last
				// written pose kept the chain wild (run 22). Put every simulated bone back on its rest pose beside
				// its anchor, at rest, and let it settle again.
				for (const auto& b : S().built) {
					for (auto& bone : b.system->getBones()) {
						auto* fb = static_cast<hdt::Fo4Bone*>(bone.get());
						if (fb->m_rig.isStaticOrKinematicObject()) {
							continue;
						}
						if (const auto it = S().rest.find(fb->m_node.get()); it != S().rest.end() && fb->m_node->parent) {
							const auto world = hdt::fo4::Compose(it->second.anchor->world, it->second.relative);
							fb->m_node->local = hdt::fo4::LocalFromWorld(fb->m_node->parent->world, world);
							fb->m_node->world = world;
						}
						fb->readTransform(hdt::RESET_PHYSICS);
					}
				}
				if (!state.isPlayer) {
					auto& times = g_resetTimes[id];
					times.push_back(a_now);
					std::erase_if(times, [&](auto t) { return std::chrono::duration<double>(a_now - t).count() > kQuarantineWindow; });
					if (static_cast<int>(times.size()) > kQuarantineResets) {
						quarantine.push_back(id);
					}
				}
			}
			for (const auto id : quarantine) {
				g_current = &g_actors[id];
				const auto until = a_now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kQuarantineSeconds));
				if (!g_noCollision.contains(id)) {
					Drop(std::format("exploded more than {} times in {:.0f} s; rebuilding without collisions for {:.0f} s", kQuarantineResets, kQuarantineWindow, kQuarantineSeconds));
					g_noCollision[id] = until;
					S().root = nullptr;  // rebuild
				} else {
					Drop(std::format("exploded more than {} times in {:.0f} s even without collisions; placed only, for {:.0f} s", kQuarantineResets, kQuarantineWindow, kQuarantineSeconds));
					g_quarantined[id] = until;
					S().simulate = false;
					S().root = nullptr;
					ClearPlacement();
				}
				g_resetTimes.erase(id);
			}
		}

		std::chrono::steady_clock::duration Seconds(double a_seconds)
		{
			return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(a_seconds));
		}

		// Diagnostic (run 33): how far a few skeleton bones move per frame, as FO4SMP reads them. The contacts that
		// fling the penis and the breasts come from kinematic leg and pelvis bones; if their world transforms are
		// only refreshed every other frame (or late), their speeds come out 0 and then double, and every contact
		// with them is a hammer blow. Logs every 10 s: per-frame displacement (units) over the last 24 frames.
		void TrackMotion(RE::NiAVObject* a_root, std::chrono::steady_clock::time_point a_now)
		{
			static RE::NiAVObject* s_root = nullptr;
			static std::array<RE::NiNode*, 4> s_nodes{};
			static std::array<RE::NiPoint3, 4> s_last{};
			static std::array<std::vector<float>, 4> s_moves;
			static int s_frames = 0, s_still[4]{};
			static auto s_next = a_now + std::chrono::seconds(10);
			static int s_logged = 0;
			constexpr std::array kNames{ "Root", "Pelvis", "RLeg_Calf", "Penis_01" };
			if (a_root != s_root) {
				s_root = a_root;
				auto* skeleton = hdt::castNiNode(a_root);
				for (std::size_t i = 0; i < kNames.size(); ++i) {
					s_nodes[i] = skeleton ? hdt::fo4::findNode(skeleton, RE::BSFixedString(kNames[i])) : nullptr;
					s_last[i] = s_nodes[i] ? s_nodes[i]->world.translate : RE::NiPoint3{};
					s_moves[i].clear();
					s_still[i] = 0;
				}
				s_frames = 0;
				return;
			}
			++s_frames;
			for (std::size_t i = 0; i < kNames.size(); ++i) {
				if (!s_nodes[i]) {
					continue;
				}
				const auto& w = s_nodes[i]->world.translate;
				const float d = std::sqrt((w.x - s_last[i].x) * (w.x - s_last[i].x) + (w.y - s_last[i].y) * (w.y - s_last[i].y) + (w.z - s_last[i].z) * (w.z - s_last[i].z));
				s_last[i] = w;
				s_moves[i].push_back(d);
				if (s_moves[i].size() > 24) {
					s_moves[i].erase(s_moves[i].begin());
				}
				s_still[i] += d == 0.0f ? 1 : 0;
			}
			if (a_now >= s_next && s_logged < 30) {
				s_next = a_now + std::chrono::seconds(10);
				++s_logged;
				std::string line;
				for (std::size_t i = 0; i < kNames.size(); ++i) {
					if (!s_nodes[i]) {
						continue;
					}
					line += std::format(" {} (unchanged in {} of {} frames):", kNames[i], s_still[i], s_frames);
					for (float m : s_moves[i]) {
						line += std::format(" {:.2f}", m);
					}
					line += ";";
					s_still[i] = 0;
				}
				s_frames = 0;
				logger::info("Player motion per frame:{}", line);
			}
		}

		// Milestone 5 groundwork (run 37): the player's head as Fallout 4 builds it. Logs every BSFaceGenNiNode subtree
		// (names, classes, string extra data such as "HDT Skinned Mesh Physics Object", skinned shapes and where their
		// bones are), and every node of the 3D parked more than 5,000 units from the root: SMP Head's face bones (not in
		// the skeleton) are suspected to sit at the world origin, which would stretch the player's bounds to ~35,000
		// units (run 37) and keep the player faded after the camera pulls in.
		void DumpHeadAndStrays(RE::NiAVObject* a_root)
		{
			const auto& r = a_root->world.translate;
			auto dist = [&](const RE::NiPoint3& a_p) {
				return std::sqrt((a_p.x - r.x) * (a_p.x - r.x) + (a_p.y - r.y) * (a_p.y - r.y) + (a_p.z - r.z) * (a_p.z - r.z));
			};
			auto extras = [](RE::NiAVObject* a_o) {
				std::string out;
				if (a_o->extra) {
					for (auto* data : *a_o->extra) {
						if (!data) {
							continue;
						}
						out += std::format(" [{} {}", hdt::fo4::ClassName(data), data->name.c_str());
						if (hdt::fo4::IsKindOf(data, ".?AVNiStringExtraData@@"sv)) {
							out += std::format(" = '{}'", static_cast<RE::NiStringExtraData*>(data)->data.c_str());
						}
						out += "]";
					}
				}
				return out;
			};
			int strays = 0;
			std::function<void(RE::NiAVObject*, int, bool)> walk = [&](RE::NiAVObject* a_o, int a_depth, bool a_inFace) {
				if (!a_o || a_depth > 40) {
					return;
				}
				const bool face = a_inFace || hdt::fo4::IsKindOf(a_o, ".?AVBSFaceGenNiNode@@"sv);
				const float d = dist(a_o->world.translate);
				if (d > 5000.0f && strays < 40) {
					++strays;
					logger::info("Head dump: stray {} '{}' (parent '{}') at {:.0f} units from the root", hdt::fo4::ClassName(a_o), a_o->name.c_str(),
						a_o->parent ? a_o->parent->name.c_str() : "", d);
				}
				if (face) {
					std::string line = std::format("Head dump: {}{} '{}' at {:.0f}{}", std::string(std::min(a_depth, 12) * 2, ' '), hdt::fo4::ClassName(a_o), a_o->name.c_str(), d, extras(a_o));
					if (auto* tri = hdt::castBSTriShape(a_o)) {
						hdt::fo4::SkinnedShape skinned;
						if (hdt::fo4::ReadSkinnedShape(tri, skinned, false).empty()) {
							line += std::format("; skinned, {} bones:", skinned.bones.size());
							for (const auto& b : skinned.bones) {
								line += b.node ? std::format(" {}@{:.0f}", b.node->name.c_str(), dist(b.node->world.translate)) : std::string{ " (null)" };
							}
						}
					}
					logger::info("{}", line);
				}
				if (auto* n = hdt::castNiNode(a_o)) {
					for (auto& child : hdt::fo4::Children(n)) {
						walk(child.get(), a_depth + 1, face);
					}
				}
			};
			walk(a_root, 0, false);
			logger::info("Head dump: done ({} stray node(s) over 5,000 units from the root)", strays);
		}

		// Diagnostic (run 36, the vanishing player): each frame, the player's 3D root and the built model's bounds and
		// culled flag; logs when either turns odd (radius over 1,000 units or not finite, center over 500 units from
		// the root, culled) and when it turns normal again, with the farthest simulated bone at that moment.
		void WatchVisibility(RE::NiAVObject* a_root, std::chrono::steady_clock::time_point)
		{
			static bool s_odd = false;
			static int s_logged = 0;
			static bool s_dumpedOnce = false;
			if (!s_dumpedOnce) {
				s_dumpedOnce = true;
				DumpHeadAndStrays(a_root);
			}
			auto describe = [&](RE::NiAVObject* a_o, const char* a_what, bool& a_bad) {
				const auto& b = a_o->worldBound;
				const auto& c = b.center;
				const auto& r = a_root->world.translate;
				const float off = std::sqrt((c.x - r.x) * (c.x - r.x) + (c.y - r.y) * (c.y - r.y) + (c.z - r.z) * (c.z - r.z));
				const bool bad = !std::isfinite(b.fRadius) || !std::isfinite(off) || b.fRadius > 1000.0f || off > 500.0f || a_o->GetAppCulled();
				a_bad |= bad;
				return std::format(" {} radius {:.0f}, center {:.0f} from the root{};", a_what, b.fRadius, off, a_o->GetAppCulled() ? ", culled" : "");
			};
			bool bad = false;
			std::string line = describe(a_root, "3D", bad);
			for (const auto& b : S().built) {
				if (b.model) {
					line += describe(b.model.get(), "model", bad);
				}
			}
			if (bad != s_odd && s_logged < 40) {
				++s_logged;
				float farthest = 0.0f;
				std::string farthestName;
				for (const auto& b : S().built) {
					for (auto& bone : b.system->getBones()) {
						auto* fb = static_cast<hdt::Fo4Bone*>(bone.get());
						if (fb->m_rig.isStaticOrKinematicObject() || !fb->m_node) {
							continue;
						}
						const auto& w = fb->m_node->world.translate;
						const auto& r = a_root->world.translate;
						const float d = std::sqrt((w.x - r.x) * (w.x - r.x) + (w.y - r.y) * (w.y - r.y) + (w.z - r.z) * (w.z - r.z));
						if (!(d <= farthest)) {
							farthest = d;
							farthestName = fb->m_name.c_str();
						}
					}
				}
				logger::warn("Player visibility: {}:{} farthest simulated bone node {} at {:.0f} units from the root",
					bad ? "bounds turned odd" : "bounds normal again", line, farthestName, farthest);
				static int s_dumps = 0;
				if (bad && s_dumps < 3) {
					++s_dumps;
					DumpHeadAndStrays(a_root);
				}
			}
			s_odd = bad;
		}

		// One actor's frame: rebuild on a new physics attach or a new 3D, keep the kinematic added bones on their
		// NIF parents, build when due, drop what's no longer attached. g_current is a_state.
		void UpdateActor(ActorState& a_state, RE::NiAVObject* a_root, std::uint32_t a_generation, const hdt::DefaultBBPList& a_defaults, std::chrono::steady_clock::time_point a_now)
		{
			g_current = &a_state;
			if (!S().simulate) {
				if (a_generation != S().generation || a_root != S().root) {
					S().generation = a_generation;
					S().root = a_root;
					ClearPlacement();
					S().nextCheck = a_now + Seconds(kScanDelaySeconds);
					return;
				}
				if (!a_root) {
					return;
				}
				if (S().placed) {
					ApplyFollow();
				} else if (a_now >= S().nextCheck) {
					if (!PlaceOnly(a_root)) {
						S().nextCheck = a_now + Seconds(kRescanSeconds);
					}
				}
				return;
			}
			// Milestone 4: a new third-person attach that names physics (outfit change, 3D reload) rebuilds once
			// the attach settles.
			if (a_generation != S().generation) {
				S().generation = a_generation;
				if (a_root == S().root) {
					Drop("a physics outfit attached");
					S().reportedEmpty = false;
					S().nextCheck = a_now + Seconds(kScanDelaySeconds);
				}
			}
			if (a_root != S().root) {
				Drop(S().isPlayer ? "the player's 3D changed" : "the NPC's 3D changed");
				S().root = a_root;
				S().rootSince = a_now;
				S().reportedEmpty = false;
				S().nextCheck = a_now + Seconds(kScanDelaySeconds);
				return;
			}
			if (a_root && !S().built.empty()) {
				ApplyFollow();  // every frame, before the physics reads the skeleton
				if (S().isPlayer) {
					TrackMotion(a_root, a_now);
					WatchVisibility(a_root, a_now);
				}
			}
			if (!a_root || a_now < S().nextCheck) {
				return;
			}

			if (S().built.empty()) {
				// NPCs: only what the attach hook saw (the scan's defaultBBPs.xml fallback is for the player).
				static const hdt::DefaultBBPList kNone{};
				Scan(a_root, S().isPlayer ? a_defaults : kNone);
				S().nextCheck = a_now + Seconds(S().waiting ? 1.0 : (S().built.empty() ? kRescanSeconds : kAttachCheckSeconds));
				return;
			}

			if (S().isPlayer) {
				static int dumps = 0;
				static auto nextDump = a_now;
				if (dumps < 12 && a_now >= nextDump) {
					for (const auto& b : S().built) {
						DumpGeometry(b);
					}
					++dumps;
					nextDump = a_now + std::chrono::seconds(dumps < 2 ? 2 : 10);
				}
			}

			for (const auto& b : S().built) {
				if (!IsAttachedUnder(b.model.get(), a_root)) {
					Drop(std::format("'{}' is no longer attached", b.model->name.c_str()));
					S().reportedEmpty = false;
					S().nextCheck = a_now + Seconds(kScanDelaySeconds);
					return;
				}
			}
			S().nextCheck = a_now + Seconds(kAttachCheckSeconds);
		}

		// Milestone 4: which NPCs are simulated: those the attach hook saw with physics, with 3D loaded, within
		// kNpcMaxDistance of the player, nearest first, up to g_maxActive - 1. The rest are dropped.
		void SelectNpcs(RE::NiAVObject* a_playerRoot, const std::vector<AttachHook::PhysicsActor>& a_actors, std::chrono::steady_clock::time_point a_now)
		{
			std::erase_if(g_quarantined, [&](const auto& e) { return a_now >= e.second; });
			std::erase_if(g_noCollision, [&](const auto& e) { return a_now >= e.second; });
			struct Near
			{
				float rank;
				float distance;
				const AttachHook::PhysicsActor* actor;
			};
			std::vector<Near> simulate, place;
			if (a_playerRoot) {
				const auto& p = a_playerRoot->world.translate;
				for (const auto& a : a_actors) {
					if (a.formID == 0x14) {
						continue;
					}
					auto* root = AttachHook::Loaded3D(a.handle);
					static std::unordered_set<std::uint32_t> s_reported;
					if (!root) {
						if (s_reported.size() < 400 && s_reported.insert(a.formID).second) {
							logger::info("NPC {:08X} physics: seen with a physics attach, but its 3D isn't loaded (handle {:08X})", a.formID, a.handle);
						}
						continue;
					}
					const auto& q = root->world.translate;
					const float d = std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z));
					if (s_reported.size() < 400 && s_reported.insert(a.formID).second) {
						logger::info("NPC {:08X} physics: seen with a physics attach, 3D '{}' ({}) {:.0f} units from the player", a.formID, root->name.c_str(), hdt::fo4::ClassName(root), d);
					}
					// Hysteresis: an NPC already simulated keeps its place until it is kNpcKeepBonus farther than
					// a newcomer, and up to kNpcMaxDistance + kNpcKeepBonus away (run 32: 303 drops and rebuilds as
					// NPCs crossed the 2,000-unit line or swapped places in the nearest four).
					const auto it = g_actors.find(a.formID);
					const bool active = it != g_actors.end() && it->second.simulate;
					if (!g_quarantined.contains(a.formID) && !g_unsimulatable.contains(a.formID) && g_maxActive > 1 && d <= kNpcMaxDistance + (active ? kNpcKeepBonus : 0.0f)) {
						simulate.push_back({ active ? std::max(0.0f, d - kNpcKeepBonus) : d, d, &a });
					} else if (d <= kPlaceDistance) {
						place.push_back({ d, d, &a });
					}
				}
				std::ranges::sort(simulate, {}, &Near::rank);
				if (simulate.size() > static_cast<std::size_t>(std::max(0, g_maxActive - 1))) {
					// The rest are placed, so their bodies keep their shape.
					place.insert(place.end(), simulate.begin() + (g_maxActive - 1), simulate.end());
					simulate.resize(g_maxActive - 1);
				}
			}

			std::map<std::uint32_t, bool> chosen;  // form ID -> simulate
			auto take = [&](const Near& a_near, bool a_simulate) {
				const auto* a = a_near.actor;
				chosen[a->formID] = a_simulate;
				auto [it, added] = g_actors.try_emplace(a->formID);
				auto& state = it->second;
				if (added) {
					state.formID = a->formID;
					state.handle = a->handle;
					state.generation = a->generation;
					state.simulate = a_simulate;
					if (a_simulate) {
						logger::info("NPC {:08X} physics: selected (about {:.0f} units from the player){}", a->formID, a_near.distance,
							g_noCollision.contains(a->formID) ? ", without collisions" : "");
					}
				} else if (state.simulate != a_simulate) {
					g_current = &state;
					Drop(a_simulate ? "now simulated" : "now only placed (not among the nearest, or quarantined)");
					ClearPlacement();
					state.simulate = a_simulate;
					state.root = nullptr;  // rebuild or re-place
					if (a_simulate) {
						logger::info("NPC {:08X} physics: selected (about {:.0f} units from the player){}", a->formID, a_near.distance,
							g_noCollision.contains(a->formID) ? ", without collisions" : "");
					}
				}
			};
			for (const auto& n : simulate) {
				take(n, true);
			}
			for (const auto& n : place) {
				take(n, false);
			}
			for (auto it = g_actors.begin(); it != g_actors.end();) {
				if (!it->second.isPlayer && !chosen.contains(it->first)) {
					g_current = &it->second;
					Drop("too far or unloaded");
					if (it->second.simulate) {
						logger::info("NPC {:08X} physics: deselected", it->first);
					}
					it = g_actors.erase(it);
				} else {
					++it;
				}
			}
		}
	}

	void SetMaxActive(int a_maxActive)
	{
		g_maxActive = std::max(1, a_maxActive);
		logger::info("Actor physics: the player and up to {} NPC(s) within {:.0f} units are simulated", g_maxActive - 1, kNpcMaxDistance);
	}

	void Update(const hdt::DefaultBBPList& a_defaults)
	{
		const auto now = std::chrono::steady_clock::now();

		auto& player = g_actors[0x14];
		player.formID = 0x14;
		player.isPlayer = true;

		const auto actors = AttachHook::PhysicsActors();
		auto generationOf = [&](std::uint32_t a_formID) {
			for (const auto& a : actors) {
				if (a.formID == a_formID) {
					return a.generation;
				}
			}
			return 0u;
		};

		auto* playerRoot = PlayerRoot();
		static auto nextSelect = now;
		if (now >= nextSelect) {
			SelectNpcs(playerRoot, actors, now);
			nextSelect = now + Seconds(kSelectSeconds);
		}

		if (hdt::Fo4Bone::TakeExplosion()) {
			ResetAfterExplosion(now);
		}

		for (auto& [id, state] : g_actors) {
			UpdateActor(state, state.isPlayer ? playerRoot : RootOf(state), generationOf(id), a_defaults, now);
		}
		g_current = &g_actors[0x14];
	}

	std::uint32_t TakeSystemResets()
	{
		const auto n = g_systemResets;
		g_systemResets = 0;
		return n;
	}

	std::string Status()
	{
		std::string out;
		int npcs = 0;
		for (const auto& [id, state] : g_actors) {
			std::size_t bones = 0, dynamic = 0;
			for (const auto& b : state.built) {
				for (auto& bone : b.system->getBones()) {
					++bones;
					dynamic += bone->m_rig.isStaticOrKinematicObject() ? 0 : 1;
				}
			}
			if (state.isPlayer) {
				out = state.built.empty() ? std::string{ "no physics on the player yet" } :
				                            std::format("player physics: {} system(s), {} bones ({} simulated)", state.built.size(), bones, dynamic);
			} else if (!state.built.empty()) {
				++npcs;
			}
		}
		return std::format("{}; {} NPC(s) simulated", out.empty() ? "no physics on the player yet" : out, npcs);
	}
}
