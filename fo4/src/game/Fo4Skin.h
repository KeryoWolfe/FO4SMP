#pragma once

// Reads a Fallout 4 skinned shape's bones, bind data, vertices and triangles from the game's memory.
//
// Layouts are F4SE's (github.com/ianpatt/f4se, f4se/BSSkin.h and BSGeometry.h at 6f6a7ca, which targets
// 1.11.240); CommonLibF4 only forward-declares BSSkin::Instance. Every pointer is range-checked and the
// result cross-checked (bone count against bind data, bone pointers against the skin's world-transform
// pointers, the renderer's vertex format against the shape's) before anything is used; a shape that
// fails a check is skipped with a log line saying which check failed.

namespace hdt::fo4
{
	struct SkinBone
	{
		RE::NiNode* node = nullptr;
		RE::NiPoint3 boundCenter;
		float boundRadius = 0.0f;
		RE::NiTransform skinToBone;
	};

	struct SkinVertex
	{
		RE::NiPoint3 position;
		float weights[4]{};
		std::uint8_t bones[4]{};
	};

	struct SkinnedShape
	{
		std::vector<SkinBone> bones;
		std::vector<SkinVertex> vertices;
		std::vector<std::uint16_t> triangles;  // 3 per triangle
		bool fullPrecision = false;
		std::uint32_t resolvedFromWorld = 0;  // null bone entries found through the world-transform pointer
		std::uint32_t unresolved = 0;         // null bone entries left null
		std::uint64_t vertexDesc = 0;
	};

	// Fills a_out from a_shape. Returns "" on success, or why the shape can't be read.
	std::string ReadSkinnedShape(RE::BSTriShape* a_shape, SkinnedShape& a_out, bool a_withTriangles);
}

namespace hdt::fo4
{
	// The skin bone names of each skinned shape in a Fallout 4 NIF (version 20.2.0.7, BS version 130), in the
	// order of the shape's BSSkin::Instance bone list: shape name -> bone names. Used for the shapes whose
	// bone entries the game leaves null at runtime (A-Body's Virtual* collision shapes). Returns "" on
	// success, or why the file couldn't be read. a_data is the whole file.
	std::string ReadNifSkinBoneNames(std::string_view a_data, std::unordered_map<std::string, std::vector<std::string>>& a_out);
}
