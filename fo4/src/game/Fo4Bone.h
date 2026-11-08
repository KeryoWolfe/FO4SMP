#pragma once

// Fallout 4 port of FSMP's src/hdtSkyrimBone.h (github.com/DaymareOn/hdtSMP64, GPL-3), adapted for
// Fallout 4's scene graph and skinning; see fo4/README.md. Changes are marked "Fallout 4:".
#include "Fo4ConvertNi.h"
#include "hdtSkinnedMesh/hdtSkinnedMeshBone.h"

namespace hdt
{
	class Fo4Bone : public SkinnedMeshBone
	{
	public:
		Fo4Bone(const RE::BSFixedString& name, RE::NiNode* node, RE::NiNode* skeleton, btRigidBody::btRigidBodyConstructionInfo& ci);

		void readTransform(float timeStep) override;
		void writeTransform() override;

		// For the frame log: the farthest simulated bone from its parent and the poses rejected by
		// writeTransform since the last call (all bones), or "".
		static std::string TakeWriteReport();
		static constexpr float kMaxDistanceFromParent = 100.0f;  // game units; A-Body is about 128 tall

		int m_depth;
		RE::NiPointer<RE::NiNode> m_node;
		RE::NiPointer<RE::NiNode> m_skeleton;

	private:
		int m_forceUpdateType;
		bool m_reportedBadWrite = false;
	};
}
