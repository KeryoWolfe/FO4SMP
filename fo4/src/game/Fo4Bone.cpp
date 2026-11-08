// Fallout 4 port of FSMP's src/hdtSkyrimBone.cpp (github.com/DaymareOn/hdtSMP64, GPL-3), adapted for
// Fallout 4's scene graph and skinning; see fo4/README.md. Changes are marked "Fallout 4:".

#include "Fo4Bone.h"

#include "hdtSkinnedMesh/hdtSkinnedMeshWorld.h"  // RESET_PHYSICS

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

	void Fo4Bone::writeTransform()
	{
		auto transform = m_rig.getWorldTransform() * m_rigToLocal;

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
		if (m_node->parent) {
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
