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

		// Diagnostic: for each added bone placed from the bind data, its anchor and its pose relative to the
		// anchor at placement, to tell how far the simulation turns it away from its rest pose.
		struct Rest
		{
			RE::NiNode* anchor;
			RE::NiTransform relative;
		};
		std::unordered_map<RE::NiNode*, Rest> g_rest;

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
		std::vector<Follow> g_follow;
		std::uint32_t g_systemResets = 0;
		RE::NiNode* g_rootBone = nullptr;
		std::chrono::steady_clock::time_point g_nextCheck{};
		bool g_reportedEmpty = false;

		// Fallout 4: the skins of shapes the game doesn't draw (A-Body's Virtual* collision shapes) start with
		// null bone entries (run 20: VirtualHands 36 of 38, VirtualArms 18 of 28) and appear to get them
		// later (run 2's second build, after an outfit change, found bones its first build hadn't). Building
		// then leaves the collision surfaces on a few bones. Wait for them, up to kBoneWaitSeconds.
		constexpr double kBoneWaitSeconds = 30.0;
		std::chrono::steady_clock::time_point g_rootSince{};
		std::vector<std::pair<RE::NiAVObject*, std::uint32_t>> g_unresolvedShapes;  // per shape, filled by Walk
		bool g_waiting = false;

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
			g_rest.clear();
			g_follow.clear();
			g_rootBone = nullptr;
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
				const auto it = g_rest.find(b->m_node.get());
				if (it == g_rest.end()) {
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
							logger::info("Player physics:   '{}' in '{}': shared skin weight with {}", node->name.c_str(), shape.name, top);
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
				g_rest[node] = { best->node, hdt::fo4::LocalFromWorld(best->node->world, world) };
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
				logger::info("Player physics: kinematic partner '{}' (parent '{}') moved {:.1f} units to '{}'", partner->name.c_str(), partner->parent->name.c_str(), moved, name);
			}
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
				g_follow.push_back({ node, anchor, hdt::fo4::LocalFromWorld(anchor->world, node->world) });
				logger::info("Player physics: kinematic added bone '{}' (under Root) now follows '{}' ({:.1f} units away)", node->name.c_str(), anchor->name.c_str(), std::sqrt(best));
			}
			g_rootBone = rootBone;
		}

		void ApplyFollow()
		{
			if (!g_rootBone) {
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
			for (const auto& f : g_follow) {
				const auto world = hdt::fo4::Compose(f.anchor->world, f.relative);
				f.node->local = hdt::fo4::LocalFromWorld(g_rootBone->world, world);
				f.node->world = world;
				follow(f.node, 0);
			}
		}

		// The NIF the physics' shapes come from, for the skin bone names the game leaves null (see
		// Fo4SystemCreator::m_nifBoneNames). Until the attach hook knows the outfit's model path (milestone 4),
		// it is found from the physics XML's name: <xml path>.nif, then
		// Meshes\Actors\Character\CharacterAssets\<xml name>.nif (A-Body's MaleBody.xml / MaleBody.nif), and
		// is used only if it has a bone list for every shape it is asked about.
		std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>> g_nifCache;

		const std::unordered_map<std::string, std::vector<std::string>>* NifBoneNames(const std::string& a_xml, const std::vector<std::string>& a_shapes)
		{
			std::filesystem::path xml{ a_xml };
			std::vector<std::string> paths;
			auto withNif = xml;
			withNif.replace_extension(".nif");
			paths.push_back(withNif.string());
			paths.push_back((std::filesystem::path{ "Meshes" } / "Actors" / "Character" / "CharacterAssets" / xml.stem()).string() + ".nif");
			for (const auto& path : paths) {
				auto it = g_nifCache.find(path);
				if (it == g_nifCache.end()) {
					std::unordered_map<std::string, std::vector<std::string>> names;
					const auto data = hdt::fo4::readAllFile(path.c_str());
					const auto error = data.empty() ? std::string{ "not found (loose files only)" } : hdt::fo4::ReadNifSkinBoneNames(data, names);
					logger::info("Player physics: NIF {}: {}", path, error.empty() ? std::format("{} skinned shape(s)", names.size()) : error);
					it = g_nifCache.emplace(path, std::move(names)).first;
				}
				if (!it->second.empty() && std::ranges::all_of(a_shapes, [&](const std::string& s) { return it->second.contains(s); })) {
					logger::info("Player physics: skin bone names from {}", path);
					return &it->second;
				}
			}
			return nullptr;
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
				if (error.empty() && skinned.unresolved) {
					g_unresolvedShapes.push_back({ shape, skinned.unresolved });
				}
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
			g_unresolvedShapes.clear();
			g_waiting = false;
			Walk(a_root, a_root, a_defaults, candidates, log, 0);
			// Only the shapes the physics will be built from count (the head's or a weapon's don't).
			std::uint32_t g_unresolved = 0;
			for (const auto& [shape, count] : g_unresolvedShapes) {
				if (std::ranges::any_of(candidates, [&](const Candidate& c) { return IsAttachedUnder(shape, c.model); })) {
					g_unresolved += count;
				}
			}

			const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_rootSince).count();
			if (!candidates.empty() && g_unresolved > 0) {
				if (false && waited < kBoneWaitSeconds) {  // the NIF's bone names fill them now (kept for reference)
					logger::info("Player physics: {} skin bone(s) of the player's shapes aren't set yet ({:.0f} s after the 3D loaded); waiting", g_unresolved, waited);
					g_waiting = true;
					g_reportedEmpty = true;  // don't list the shapes again
					return;
				}
				logger::info("Player physics: {} skin bone(s) of the physics' shapes are unset ({:.0f} s after the 3D loaded); filling them from the NIF", g_unresolved, waited);
			} else if (!candidates.empty()) {
				logger::info("Player physics: all skin bones set ({:.0f} s after the 3D loaded)", waited);
			}

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
				FixAddedBones(skeleton, c.model);

				hdt::DefaultBBP::PhysicsFile_t file{ c.file, {} };
				hdt::Fo4SystemCreator creator;
				{
					// The shapes under the model with null skin bones.
					std::vector<std::string> shapes;
					for (const auto& [shape, count] : g_unresolvedShapes) {
						if (IsAttachedUnder(shape, c.model)) {
							shapes.emplace_back(shape->name.c_str());
						}
					}
					if (!shapes.empty()) {
						creator.m_nifBoneNames = NifBoneNames(c.file, shapes);
						if (!creator.m_nifBoneNames) {
							logger::warn("Player physics: no NIF found with bone lists for the {} shape(s) with null skin bones; their collision will be wrong", shapes.size());
						}
					}
				}
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

				BuildFollow(skeleton, system.get());
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
			g_rootSince = now;
			g_reportedEmpty = false;
			g_nextCheck = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kScanDelaySeconds));
			return;
		}
		if (root && !g_built.empty()) {
			ApplyFollow();  // every frame, before the physics reads the skeleton
			// A rejected (exploded) pose leaves its neighbours strained; resetting one bone onto its last
			// written pose kept the chain wild (run 22: ~2,900 rejections per 10 s). Put every simulated bone
			// back on its rest pose beside its anchor, at rest, and let it settle again.
			if (hdt::Fo4Bone::TakeExplosion()) {
				++g_systemResets;
				for (const auto& b : g_built) {
					for (auto& bone : b.system->getBones()) {
						auto* fb = static_cast<hdt::Fo4Bone*>(bone.get());
						if (fb->m_rig.isStaticOrKinematicObject()) {
							continue;
						}
						if (const auto it = g_rest.find(fb->m_node.get()); it != g_rest.end() && fb->m_node->parent) {
							const auto world = hdt::fo4::Compose(it->second.anchor->world, it->second.relative);
							fb->m_node->local = hdt::fo4::LocalFromWorld(fb->m_node->parent->world, world);
							fb->m_node->world = world;
						}
						fb->readTransform(hdt::RESET_PHYSICS);
					}
				}
			}
		}
		if (!root || now < g_nextCheck) {
			return;
		}

		if (g_built.empty()) {
			Scan(root, a_defaults);
			g_nextCheck = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(g_waiting ? 1.0 : (g_built.empty() ? kRescanSeconds : kAttachCheckSeconds)));
			return;
		}

		static int dumps = 0;
		static auto nextDump = now;
		if (dumps < 12 && now >= nextDump) {
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

	std::uint32_t TakeSystemResets()
	{
		const auto n = g_systemResets;
		g_systemResets = 0;
		return n;
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
