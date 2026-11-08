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

		int m_depth;
		RE::NiPointer<RE::NiNode> m_node;
		RE::NiPointer<RE::NiNode> m_skeleton;

	private:
		int m_forceUpdateType;
	};
}
