#include "Fo4Skin.h"

#include "Fo4NiUtils.h"

namespace hdt::fo4
{
	namespace
	{
		// F4SE's tArray (= CommonLibF4's BSTArray): data, capacity, pad, size, pad.
		struct RawArray
		{
			void* data;
			std::uint32_t capacity;
			std::uint32_t pad0C;
			std::uint32_t size;
			std::uint32_t pad14;
		};
		static_assert(sizeof(RawArray) == 0x18);

		// BSSkin::BoneData::BoneTransforms: bounding sphere (center, radius), then the skin-to-bone transform.
		struct RawBoneTransform
		{
			RE::NiPoint3 center;      // 00
			float radius;             // 0C
			RE::NiTransform transform;  // 10
		};
		static_assert(sizeof(RE::NiTransform) == 0x40);
		static_assert(sizeof(RawBoneTransform) == 0x50);

		// Offsets inside BSSkin::Instance and BSSkin::BoneData (F4SE BSSkin.h).
		constexpr std::size_t kInstanceBones = 0x10;            // tArray<NiNode*>
		constexpr std::size_t kInstanceWorldTransforms = 0x28;  // tArray<NiTransform*>
		constexpr std::size_t kInstanceBoneData = 0x40;         // BoneData*
		constexpr std::size_t kBoneDataTransforms = 0x10;       // tArray<BoneTransforms>

		// BSGeometry (F4SE BSGeometry.h; CommonLibF4 agrees on 0x140 and 0x148).
		constexpr std::size_t kGeometrySkinInstance = 0x140;
		constexpr std::size_t kGeometryRendererData = 0x148;  // BSGraphics::TriShape*
		constexpr std::size_t kGeometryVertexDesc = 0x150;
		constexpr std::size_t kTriShapeNumTriangles = 0x160;
		constexpr std::size_t kTriShapeNumVertices = 0x164;

		// BSGraphics::TriShape { u64 vertexDesc; VertexBuffer* vb; IndexBuffer* ib; } ; Buffer { ID3D11Buffer*; void* pData; ... }
		constexpr std::size_t kRendererVertexDesc = 0x00;
		constexpr std::size_t kRendererVB = 0x08;
		constexpr std::size_t kRendererIB = 0x10;
		constexpr std::size_t kBufferData = 0x08;

		constexpr std::uint64_t kFlagSkinned = 1ull << 50;
		constexpr std::uint64_t kFlagFullPrecision = 1ull << 54;

		template <class T>
		T Read(std::uintptr_t a_address, std::size_t a_offset) noexcept
		{
			T value;
			std::memcpy(&value, reinterpret_cast<const void*>(a_address + a_offset), sizeof(T));
			return value;
		}

		bool Plausible(std::uintptr_t a_pointer) noexcept
		{
			return a_pointer >= 0x10000 && a_pointer <= kCanonicalUserSpaceMax;
		}

		float Half(std::uint16_t a_half) noexcept
		{
			const std::uint32_t sign = (a_half & 0x8000u) << 16;
			const std::uint32_t exponent = (a_half >> 10) & 0x1Fu;
			const std::uint32_t mantissa = a_half & 0x3FFu;
			std::uint32_t bits;
			if (exponent == 0) {
				bits = sign;  // zero (denormals flushed, as FSMP's own conversion does)
			} else if (exponent == 31) {
				bits = sign | 0x7F800000u | (mantissa << 13);
			} else {
				bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
			}
			float f;
			std::memcpy(&f, &bits, sizeof(f));
			return f;
		}
	}

	std::string ReadSkinnedShape(RE::BSTriShape* a_shape, SkinnedShape& a_out, bool a_withTriangles)
	{
		const auto shape = reinterpret_cast<std::uintptr_t>(a_shape);
		a_out = {};

		// --- skin instance and bones ---
		const auto skin = Read<std::uintptr_t>(shape, kGeometrySkinInstance);
		if (!skin) {
			return "not skinned (no skin instance)";
		}
		if (!Plausible(skin) || !IsKindOf(reinterpret_cast<void*>(skin), ".?AVInstance@BSSkin@@"sv)) {
			return std::format("skin instance at {:X} isn't a BSSkin::Instance ({})", skin, ClassName(reinterpret_cast<void*>(skin)));
		}

		const auto bones = Read<RawArray>(skin, kInstanceBones);
		const auto worlds = Read<RawArray>(skin, kInstanceWorldTransforms);
		const auto boneData = Read<std::uintptr_t>(skin, kInstanceBoneData);
		if (bones.size == 0 || bones.size > 1024 || !Plausible(reinterpret_cast<std::uintptr_t>(bones.data))) {
			return std::format("implausible bone list (count {})", bones.size);
		}
		if (!Plausible(boneData) || !IsKindOf(reinterpret_cast<void*>(boneData), ".?AVBoneData@BSSkin@@"sv)) {
			return std::format("bone data at {:X} isn't a BSSkin::BoneData ({})", boneData, ClassName(reinterpret_cast<void*>(boneData)));
		}
		const auto transforms = Read<RawArray>(boneData, kBoneDataTransforms);
		if (transforms.size != bones.size || !Plausible(reinterpret_cast<std::uintptr_t>(transforms.data))) {
			return std::format("bone data has {} transforms for {} bones", transforms.size, bones.size);
		}

		a_out.bones.resize(bones.size);
		const auto* boneNodes = static_cast<RE::NiAVObject* const*>(bones.data);
		const auto* boneTransforms = static_cast<const RawBoneTransform*>(transforms.data);
		const auto* worldPointers = worlds.size == bones.size && Plausible(reinterpret_cast<std::uintptr_t>(worlds.data)) ?
		                                static_cast<RE::NiTransform* const*>(worlds.data) :
		                                nullptr;
		for (std::uint32_t i = 0; i < bones.size; ++i) {
			auto* node = castNiNode(boneNodes[i]);
			if (boneNodes[i] && !node) {
				return std::format("bone {} isn't a node ({})", i, ClassName(boneNodes[i]));
			}
			if (node && worldPointers && worldPointers[i] != &node->world) {
				return std::format("bone {} ({}): the skin's world-transform pointer isn't the bone's own", i, node->name.c_str());
			}
			auto& out = a_out.bones[i];
			out.node = node;
			out.boundCenter = boneTransforms[i].center;
			out.boundRadius = boneTransforms[i].radius;
			out.skinToBone = boneTransforms[i].transform;
		}

		// --- vertex data (the renderer's CPU copy) ---
		const auto desc = Read<std::uint64_t>(shape, kGeometryVertexDesc);
		const auto renderer = Read<std::uintptr_t>(shape, kGeometryRendererData);
		if (!Plausible(renderer)) {
			return "no renderer data";
		}
		if (Read<std::uint64_t>(renderer, kRendererVertexDesc) != desc) {
			return std::format("renderer vertex format {:016X} doesn't match the shape's {:016X}", Read<std::uint64_t>(renderer, kRendererVertexDesc), desc);
		}
		if (!(desc & kFlagSkinned)) {
			return std::format("vertex format {:016X} has no skinning data", desc);
		}
		const auto vb = Read<std::uintptr_t>(renderer, kRendererVB);
		const auto vertexData = Plausible(vb) ? Read<std::uintptr_t>(vb, kBufferData) : 0;
		if (!Plausible(vertexData)) {
			return "the vertex buffer has no CPU copy";
		}

		const std::uint32_t vertexSize = static_cast<std::uint32_t>(desc & 0xF) * 4;
		const std::uint32_t skinOffset = static_cast<std::uint32_t>((desc >> 28) & 0xF) * 4;
		const auto vertexCount = Read<std::uint16_t>(shape, kTriShapeNumVertices);
		a_out.vertexDesc = desc;
		a_out.fullPrecision = (desc & kFlagFullPrecision) != 0;
		if (vertexSize == 0 || skinOffset + 12 > vertexSize || vertexCount == 0) {
			return std::format("unexpected vertex layout (size {}, skin data at {}, {} vertices)", vertexSize, skinOffset, vertexCount);
		}

		a_out.vertices.resize(vertexCount);
		for (std::uint32_t v = 0; v < vertexCount; ++v) {
			const auto base = vertexData + static_cast<std::uintptr_t>(v) * vertexSize;
			auto& out = a_out.vertices[v];
			if (a_out.fullPrecision) {
				out.position = { Read<float>(base, 0), Read<float>(base, 4), Read<float>(base, 8) };
			} else {
				out.position = { Half(Read<std::uint16_t>(base, 0)), Half(Read<std::uint16_t>(base, 2)), Half(Read<std::uint16_t>(base, 4)) };
			}
			for (int k = 0; k < 4; ++k) {
				out.weights[k] = Half(Read<std::uint16_t>(base, skinOffset + 2 * k));
				out.bones[k] = Read<std::uint8_t>(base, skinOffset + 8 + k);
				if (out.bones[k] >= bones.size) {
					out.weights[k] = 0.0f;
					out.bones[k] = 0;
				}
			}
		}

		// --- triangles ---
		if (a_withTriangles) {
			const auto triangleCount = Read<std::uint32_t>(shape, kTriShapeNumTriangles);
			const auto ib = Read<std::uintptr_t>(renderer, kRendererIB);
			const auto indexData = Plausible(ib) ? Read<std::uintptr_t>(ib, kBufferData) : 0;
			if (!Plausible(indexData)) {
				return "the index buffer has no CPU copy";
			}
			if (triangleCount == 0 || triangleCount > 1'000'000) {
				return std::format("implausible triangle count {}", triangleCount);
			}
			a_out.triangles.resize(static_cast<std::size_t>(triangleCount) * 3);
			std::memcpy(a_out.triangles.data(), reinterpret_cast<const void*>(indexData), a_out.triangles.size() * sizeof(std::uint16_t));
			for (auto index : a_out.triangles) {
				if (index >= vertexCount) {
					return std::format("triangle index {} out of range ({} vertices)", index, vertexCount);
				}
			}
		}

		return {};
	}
}
