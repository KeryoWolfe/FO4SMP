// Fallout 4 port of FSMP's src/hdtSkyrimBone.cpp (github.com/DaymareOn/hdtSMP64, GPL-3), adapted for
// Fallout 4's scene graph and skinning; see fo4/README.md. Changes are marked "Fallout 4:".

#include "Fo4Bone.h"

#include "hdtSkinnedMesh/hdtSkinnedMeshWorld.h"  // RESET_PHYSICS

#include <cmath>

namespace hdt
{
	Fo4Bone::Fo4Bone(const RE::BSFixedString& name, RE::NiNode* node, RE::NiNode* skeleton, btRigidBody::btRigidBodyConstructionInfo& ci) :
		SkinnedMeshBone(name, ci), m_node(node), m_skeleton(skeleton)
	{
		if (ci.m_mass)
			m_rig.setCollisionFlags(0);

		else
			m_rig.setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);

		m_depth = 0;
		for (auto i = node; i; i = i->parent)
			++m_depth;

		// FSMP's force-update list names Skyrim's weapon nodes; Fallout 4 has none of them.
		this->m_forceUpdateType = 0;
	}

	void Fo4Bone::readTransform(float timeStep)
	{
		auto oldScale = m_currentTransform.getScale();

		m_currentTransform = convertNi(m_node->world);

		auto newScale = m_currentTransform.getScale();
		auto current = m_rig.getWorldTransform();
		auto isStaticOrKinematic = m_rig.isStaticOrKinematicObject();
		auto scaleChanged = !btFuzzyZero(newScale - oldScale);

		if (scaleChanged) {
			auto factor = oldScale / newScale;
			if (!isStaticOrKinematic) {
				auto factor2 = factor * factor;
				auto factor3 = factor2 * factor;
				auto factor5 = factor3 * factor2;
				auto inertia = m_rig.getInvInertiaDiagLocal();
				m_rig.setMassProps(1.0f / (m_rig.getInvMass() * factor3), btVector3(1, 1, 1));
				m_rig.setInvInertiaDiagLocal(inertia * factor5);
				m_rig.updateInertiaTensor();
			}
			auto invFactor = 1.0f / factor;
			m_localToRig.getOrigin() *= invFactor;
			m_rigToLocal.getOrigin() *= invFactor;
			m_rig.getCollisionShape()->setLocalScaling(setAll(newScale));
		}

		auto dest = m_currentTransform.asTransform() * m_localToRig;
		if (timeStep <= RESET_PHYSICS) {
			static const btVector3 zero(0, 0, 0);
			m_rig.setWorldTransform(dest);
			m_rig.setInterpolationWorldTransform(dest);
			m_rig.setLinearVelocity(zero);
			m_rig.setAngularVelocity(zero);
			m_rig.setInterpolationLinearVelocity(zero);
			m_rig.setInterpolationAngularVelocity(zero);
			m_rig.updateInertiaTensor();
			//auto det = dest.getBasis().determinant();
			//if (det < FLT_EPSILON || isnan(det) || isinf(det))
			//	_WARNING("Invalid rotation matrix!!");
			//det = m_rig.getInvInertiaTensorWorld().determinant();
			//if (isnan(det) || isinf(det))
			//	_WARNING("Invalid inertia tensor matrix!!");
		} else if (isStaticOrKinematic) {
			btVector3 linVel, angVel;
			btTransformUtil::calculateVelocity(current, dest, timeStep, linVel, angVel);
			m_rig.setLinearVelocity(linVel);
			m_rig.setAngularVelocity(angVel);
			m_rig.setInterpolationLinearVelocity(linVel);
			m_rig.setInterpolationAngularVelocity(angVel);
		}
		//else
		//{
		//	auto det = m_rig.getWorldTransform().getBasis().determinant();
		//	if (isnan(det))
		//	{
		//		_WARNING("Invalid world transform");
		//		m_rig.setWorldTransform(dest);
		//		m_rig.setInterpolationWorldTransform(dest);
		//		m_rig.setLinearVelocity(btVector3(0, 0, 0));
		//		m_rig.setAngularVelocity(btVector3(0, 0, 0));
		//		m_rig.setInterpolationLinearVelocity(btVector3(0, 0, 0));
		//		m_rig.setInterpolationAngularVelocity(btVector3(0, 0, 0));
		//		m_rig.updateInertiaTensor();
		//	}
		//}
	}

	namespace
	{
		bool Finite(const btTransform& a_t)
		{
			const auto& b = a_t.getBasis();
			const auto& o = a_t.getOrigin();
			for (int i = 0; i < 3; ++i) {
				if (!std::isfinite(o[i]) || !std::isfinite(b[i][0]) || !std::isfinite(b[i][1]) || !std::isfinite(b[i][2])) {
					return false;
				}
			}
			return true;
		}

		// Written on the main thread only (SkinnedMeshWorld::writeTransform is sequential).
		std::uint32_t g_rejectedWrites = 0;
		bool g_explosion = false;
		float g_maxDistance = 0.0f;
		std::string g_maxDistanceBone;
	}

	bool Fo4Bone::s_worldOnly = false;

	bool Fo4Bone::TakeExplosion()
	{
		const bool e = g_explosion;
		g_explosion = false;
		return e;
	}

	std::string Fo4Bone::TakeWriteReport()
	{
		std::string report;
		if (!g_maxDistanceBone.empty()) {
			report = std::format("; farthest simulated bone {:.1f} units from its parent ({})", g_maxDistance, g_maxDistanceBone);
		}
		if (g_rejectedWrites) {
			report += std::format("; {} bad simulated bone pose(s) rejected", g_rejectedWrites);
		}
		g_rejectedWrites = 0;
		g_maxDistance = 0.0f;
		g_maxDistanceBone.clear();
		return report;
	}

	void Fo4Bone::writeTransform()
	{
		auto transform = m_rig.getWorldTransform() * m_rigToLocal;

		// Fallout 4 (diagnostic and guard): a simulated bone that comes out of the step non-finite or far
		// from its parent would drag its vertices across the map. Don't write it; put it back on the
		// animated pose, and say which bone it was (once per bone).
		if (m_node->parent) {
			const auto& p = m_node->parent->world.translate;
			const btVector3 parentPos(p.x, p.y, p.z);
			const float distance = Finite(transform) ? (transform.getOrigin() - parentPos).length() : -1.0f;
			if (distance < 0.0f || distance > kMaxDistanceFromParent) {
				++g_rejectedWrites;
				g_explosion = true;
				// The first 60 rejections are logged (any bone), with the speed the bone had, to see how explosions start.
				static int s_logged = 0;
				if (s_logged < 60) {
					++s_logged;
					const auto& o = transform.getOrigin();
					logger::warn("Bone {}: simulated pose {} (at {:.1f}, {:.1f}, {:.1f}, parent '{}' at {:.1f}, {:.1f}, {:.1f}), speed {:.0f} units/s, spin {:.1f} rad/s; not written, reset ({} of 60 logged)",
						m_name.c_str(), distance < 0.0f ? "isn't finite" : std::format("is {:.0f} units from its parent", distance),
						o.x(), o.y(), o.z(), m_node->parent->name.c_str(), p.x, p.y, p.z,
						m_rig.getLinearVelocity().length(), m_rig.getAngularVelocity().length(), s_logged);
				}
				readTransform(RESET_PHYSICS);
				return;
			}
			if (distance > g_maxDistance) {
				g_maxDistance = distance;
				g_maxDistanceBone = m_name.c_str();
			}
		}

		m_currentTransform.setBasis(transform.getBasis());
		m_currentTransform.setOrigin(transform.getOrigin());

		RE::NiTransform world = m_node->world;
		world.rotate = convertBt(transform.getBasis());
		world.translate = convertBt(transform.getOrigin());

		// Fallout 4: FO4SMP steps after the game's frame (Main::OnIdle), so the engine's next update
		// recomputes world transforms from local ones before anything is drawn; a world transform written
		// here alone would be lost. Write the local transform that gives this world transform under the
		// current parent, so the pose survives that update (one frame behind the parent's own motion).
		// The world transform is set too, so the children updated below follow at once.
		if (m_node->parent && !s_worldOnly) {
			m_node->local = fo4::LocalFromWorld(m_node->parent->world, world);
		}
		m_node->world = world;
		updateTransformUpDown(m_node.get(), false);
	}

	//void Fo4Bone::debugPrint(std::string name) {
	//	if (this->m_name == name && SkyrimPhysicsWorld::get()->isSuspended() == false) {
	//		auto tf0 = m_rig.getWorldTransform().getOrigin();
	//		auto tf = (convertNi(m_skeleton->m_worldTransform).inverse() * convertNi(m_node->m_worldTransform)).getOrigin();
	//		auto tf1 = (convertNi(m_node->m_parent->m_parent->m_worldTransform).inverse() * convertNi(m_node->m_worldTransform)).getOrigin();

	//		Console_Print("wrote transforms bone %s [%.3f, %.3f, %.3f] | [%.3f, %.3f, %.3f] | [%.3f, %.3f, %.3f], %d, Kinematic: %s", m_node->m_name, tf0.x(), tf0.y(), tf0.z(), tf.x(), tf.y(), tf.z(), tf1.x(), tf1.y(), tf1.z(), clock(), m_rig.isStaticOrKinematicObject() ? "true" : "false");
	//	}
	//}
}
