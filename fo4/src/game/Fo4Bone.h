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

		// True if a simulated pose was rejected since the last call (PlayerPhysics then resets the system).
		static bool TakeExplosion();

		// Diagnostic (F4SE\Plugins\FO4SMP\worldonly.txt): write only the world transform, not the local one.
		// If the physics still shows, the frame is drawn from what FO4SMP writes; if the soft parts go stiff,
		// it is drawn after the engine's next update recomputes world transforms from local ones.
		static bool s_worldOnly;
		static constexpr float kMaxDistanceFromParent = 500.0f;  // game units; A-Body's are parented to Root, at the feet, and the body is about 128 tall

		int m_depth;
		RE::NiPointer<RE::NiNode> m_node;
		RE::NiPointer<RE::NiNode> m_skeleton;

	private:
		int m_forceUpdateType;
		bool m_reportedBadWrite = false;
	};
}
