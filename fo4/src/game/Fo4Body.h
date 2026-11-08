#pragma once

// Fallout 4 port of FSMP's src/hdtSkyrimBody.h (github.com/DaymareOn/hdtSMP64, GPL-3), adapted for
// Fallout 4's scene graph and skinning; see fo4/README.md. Changes are marked "Fallout 4:".
#include "Fo4ConvertNi.h"
#include "hdtSkinnedMesh/hdtSkinnedMeshBody.h"
#include "Fo4Bone.h"

namespace hdt
{
	class Fo4System;

	class Fo4Body : public SkinnedMeshBody
	{
	public:
		Fo4Body();
		~Fo4Body();

		enum class SharedType
		{
			SHARED_PUBLIC,
			SHARED_INTERNAL,
			SHARED_EXTERNAL,
			SHARED_PRIVATE,
		};

		Fo4System* m_mesh;
		SharedType m_shared;
		bool m_disabled = false;
		int m_disablePriority = 0;
		RE::BSFixedString m_disableTag;

		bool canCollideWith(const SkinnedMeshBody* body) const override;
		void internalUpdate() override;
	};
}
