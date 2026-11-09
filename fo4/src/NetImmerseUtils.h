#pragma once

// Fallout 4 stand-in for FSMP's src/NetImmerseUtils.h (Skyrim's NetImmerse helpers), found first on FO4SMP's include
// path. FSMP's validator only needs readAllFile2 from it; here that reads like the rest of FO4SMP: a loose file
// relative to Data, else the game's archives.

#include "game/Fo4NiUtils.h"

namespace hdt
{
	inline std::string readAllFile2(const char* a_path)
	{
		return fo4::readAllFile(a_path);
	}
}
