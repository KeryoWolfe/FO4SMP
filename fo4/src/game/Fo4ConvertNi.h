#pragma once

// Fallout 4 port of FSMP's src/hdtConvertNi.h (github.com/DaymareOn/hdtSMP64, GPL-3), adapted for
// Fallout 4's scene graph and skinning; see fo4/README.md. Changes are marked "Fallout 4:".
#include "Fo4NiUtils.h"
#include "hdtSkinnedMesh/hdtBulletHelper.h"

namespace hdt
{
	btQuaternion convertNi(const RE::NiMatrix3& rhs);

	inline btVector3 convertNi(const RE::NiPoint3& rhs)
	{
		return btVector3(rhs.x, rhs.y, rhs.z);
	}

	inline btQsTransform convertNi(const RE::NiTransform& rhs)
	{
		btQsTransform ret;
		ret.setBasis(convertNi(rhs.rotate));
		ret.setOrigin(convertNi(rhs.translate));
		ret.setScale(rhs.scale);
		return ret;
	}

	RE::NiPoint3 convertBt(const btVector3& rhs);
	RE::NiMatrix3 convertBt(const btMatrix3x3& rhs);
	RE::NiMatrix3 convertBt(const btQuaternion& rhs);
	RE::NiTransform convertBt(const btQsTransform& rhs);

	static const float scaleRealWorld = 0.01425f;
	static const float scaleSkyrim = 1 / scaleRealWorld;
}
