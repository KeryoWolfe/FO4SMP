// Fallout 4 port of FSMP's src/hdtConvertNi.cpp (github.com/DaymareOn/hdtSMP64, GPL-3), adapted for
// Fallout 4's scene graph and skinning; see fo4/README.md. Changes are marked "Fallout 4:".

#include "Fo4ConvertNi.h"

namespace hdt
{
	btQuaternion convertNi(const RE::NiMatrix3& rhs)
	{
		//using namespace DirectX;
		//auto mat = XMLoadFloat3x3((const XMFLOAT3X3*)&rhs);
		//auto q = XMQuaternionRotationMatrix(XMMatrixTranspose(mat));
		//if (XMQuaternionIsInfinite(q) || XMQuaternionIsNaN(q))
		//	return XMQuaternionIdentity();
		//return q;
		// Fallout 4: the engine stores rotations transposed relative to Skyrim (row vectors: its
		// NiTransform multiply at 1.11.240 +0344820 computes world.rotate = local.rotate * parent.rotate),
		// so Bullet's column-vector matrix is the transpose of the stored one.
		btMatrix3x3 mat;
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				mat[i][j] = rhs.entry[j][i];
			}
		}
		btQuaternion q;
		mat.getRotation(q);
		return q;
	}

	RE::NiTransform convertBt(const btQsTransform& rhs)
	{
		RE::NiTransform ret;

		ret.rotate = convertBt(btMatrix3x3(rhs.getBasis()));
		ret.translate = convertBt(rhs.getOrigin());
		ret.scale = rhs.getScale();

		return ret;
	}

	RE::NiMatrix3 convertBt(const btQuaternion& rhs)
	{
		btMatrix3x3 mat(rhs.normalized());
		return convertBt(mat);
	}

	RE::NiPoint3 convertBt(const btVector3& rhs)
	{
		RE::NiPoint3 ret;
		ret.x = rhs[0];
		ret.y = rhs[1];
		ret.z = rhs[2];
		return ret;
	}

	RE::NiMatrix3 convertBt(const btMatrix3x3& rhs)
	{
		// Fallout 4: stored transposed, see convertNi.
		RE::NiMatrix3 ret;
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				ret.entry[i][j] = rhs[j][i];
			}
			ret.entry[i][3] = 0.0f;
		}
		return ret;
	}
}
