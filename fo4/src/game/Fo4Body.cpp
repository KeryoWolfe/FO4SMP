// Fallout 4 port of FSMP's src/hdtSkyrimBody.cpp (github.com/DaymareOn/hdtSMP64, GPL-3), adapted for
// Fallout 4's scene graph and skinning; see fo4/README.md. Changes are marked "Fallout 4:".

#include "Fo4Body.h"
#include "Fo4System.h"

namespace hdt
{
	Fo4Body::Fo4Body()
	{
		m_mesh = nullptr;
		m_shared = SharedType::SHARED_PUBLIC;
	}

	Fo4Body::~Fo4Body()
	{
	}

	bool Fo4Body::canCollideWith(const SkinnedMeshBody* rhs) const
	{
		auto body = (Fo4Body*)rhs;
		if (m_disabled || body->m_disabled)
			return false;

		switch (m_shared) {
		case SharedType::SHARED_PUBLIC:
			break;
		case SharedType::SHARED_INTERNAL:
			if (m_mesh->m_skeleton != body->m_mesh->m_skeleton)
				return false;
			break;
		case SharedType::SHARED_EXTERNAL:
			if (m_mesh->m_skeleton == body->m_mesh->m_skeleton)
				return false;
			break;
		case SharedType::SHARED_PRIVATE:
			if (m_mesh != body->m_mesh)
				return false;
			break;
		default:
			return false;
		}

		return SkinnedMeshBody::canCollideWith(rhs);
	}

	void Fo4Body::internalUpdate()
	{
		if (m_disabled)
			return;
		SkinnedMeshBody::internalUpdate();
	}
}
