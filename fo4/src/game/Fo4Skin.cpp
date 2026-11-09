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

	std::string RebindSkinBone(RE::BSTriShape* a_shape, std::uint32_t a_index, RE::NiNode* a_anchor, const RE::NiTransform& a_local,
		std::set<std::pair<std::uintptr_t, std::uint32_t>>& a_adjusted)
	{
		const auto shape = reinterpret_cast<std::uintptr_t>(a_shape);
		const auto skin = Read<std::uintptr_t>(shape, kGeometrySkinInstance);
		if (!Plausible(skin) || !IsKindOf(reinterpret_cast<void*>(skin), ".?AVInstance@BSSkin@@"sv)) {
			return "no skin instance";
		}
		auto& bones = *reinterpret_cast<RawArray*>(skin + kInstanceBones);
		auto& worlds = *reinterpret_cast<RawArray*>(skin + kInstanceWorldTransforms);
		const auto boneData = Read<std::uintptr_t>(skin, kInstanceBoneData);
		if (a_index >= bones.size || !Plausible(reinterpret_cast<std::uintptr_t>(bones.data))) {
			return "bone index out of range";
		}
		if (!Plausible(boneData) || !IsKindOf(reinterpret_cast<void*>(boneData), ".?AVBoneData@BSSkin@@"sv)) {
			return "no bone data";
		}
		auto& transforms = *reinterpret_cast<RawArray*>(boneData + kBoneDataTransforms);
		if (a_index >= transforms.size || !Plausible(reinterpret_cast<std::uintptr_t>(transforms.data))) {
			return "bone data index out of range";
		}
		if (a_adjusted.insert({ boneData, a_index }).second) {
			auto& t = static_cast<RawBoneTransform*>(transforms.data)[a_index];
			t.transform = Compose(a_local, t.transform);
			RE::NiTransform center = Identity();
			center.translate = t.center;
			t.center = Compose(a_local, center).translate;
			t.radius *= a_local.scale;
		}
		static_cast<RE::NiAVObject**>(bones.data)[a_index] = a_anchor;
		if (worlds.size == bones.size && Plausible(reinterpret_cast<std::uintptr_t>(worlds.data))) {
			static_cast<RE::NiTransform**>(worlds.data)[a_index] = &a_anchor->world;
		}
		return {};
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
			// Fallout 4: for shapes the game doesn't draw (A-Body's Virtual* collision shapes), many entries of
			// the bone list stay null while the skin's world-transform pointer is filled in (run 19: up to
			// 1,418 of a shape's vertices had no weight on a known bone). When that pointer is a node's own
			// world transform (NiAVObject::world, +0x70), that node is the bone.
			if (!node && worldPointers && worldPointers[i]) {
				const auto candidate = reinterpret_cast<std::uintptr_t>(worldPointers[i]) - 0x70;
				if (Plausible(candidate) && IsKindOf(reinterpret_cast<void*>(candidate), ".?AVNiNode@@"sv)) {
					node = reinterpret_cast<RE::NiNode*>(candidate);
					++a_out.resolvedFromWorld;
				} else {
					++a_out.unresolved;
				}
			} else if (!node) {
				++a_out.unresolved;
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

namespace hdt::fo4
{
	namespace
	{
		struct NifReader
		{
			std::string_view data;
			std::size_t pos = 0;
			bool ok = true;

			template <class T>
			T Get()
			{
				T value{};
				if (pos + sizeof(T) > data.size()) {
					ok = false;
					return value;
				}
				std::memcpy(&value, data.data() + pos, sizeof(T));
				pos += sizeof(T);
				return value;
			}

			std::string Bytes(std::size_t a_count)
			{
				if (pos + a_count > data.size()) {
					ok = false;
					return {};
				}
				std::string s{ data.substr(pos, a_count) };
				pos += a_count;
				return s;
			}

			std::string SizedString() { return Bytes(Get<std::uint32_t>()); }
			void ExportString() { Bytes(Get<std::uint8_t>()); }
		};
	}

	namespace
	{
		struct NifFile
		{
			std::vector<std::string> types;
			std::vector<std::uint16_t> typeIndex;
			std::vector<std::uint32_t> sizes;
			std::vector<std::string> strings;
			std::vector<std::size_t> offsets;
			std::uint32_t numBlocks = 0;

			std::string_view TypeOf(std::int32_t a_block) const
			{
				return a_block >= 0 && static_cast<std::uint32_t>(a_block) < numBlocks && typeIndex[a_block] < types.size() ? std::string_view{ types[typeIndex[a_block]] } : std::string_view{};
			}
		};

		// Header (nif.xml, 20.2.0.7 / Bethesda stream header) and block offsets.
		std::string ParseNif(std::string_view a_data, NifFile& f)
		{
			NifReader r{ a_data };
			const auto eol = a_data.find('\n');
			if (eol == std::string_view::npos || !a_data.starts_with("Gamebryo File Format")) {
				return "not a NIF";
			}
			r.pos = eol + 1;
			const auto version = r.Get<std::uint32_t>();
			if (version != 0x14020007) {
				return std::format("NIF version {:08X}, expected 14020007", version);
			}
			r.Get<std::uint8_t>();  // endian
			const auto userVersion = r.Get<std::uint32_t>();
			f.numBlocks = r.Get<std::uint32_t>();
			if (userVersion < 3) {
				return std::format("user version {}, expected a Bethesda stream header", userVersion);
			}
			const auto bsVersion = r.Get<std::uint32_t>();
			r.ExportString();  // author
			if (bsVersion > 130) {
				r.Get<std::uint32_t>();
			} else {
				r.ExportString();  // process script
			}
			r.ExportString();  // export script
			if (bsVersion >= 103) {
				r.ExportString();  // max filepath
			}
			f.types.resize(r.Get<std::uint16_t>());
			for (auto& t : f.types) {
				t = r.SizedString();
			}
			f.typeIndex.resize(f.numBlocks);
			for (auto& t : f.typeIndex) {
				t = r.Get<std::uint16_t>() & 0x7FFF;
			}
			f.sizes.resize(f.numBlocks);
			for (auto& s : f.sizes) {
				s = r.Get<std::uint32_t>();
			}
			f.strings.resize(r.Get<std::uint32_t>());
			r.Get<std::uint32_t>();  // max string length
			for (auto& s : f.strings) {
				s = r.SizedString();
			}
			const auto numGroups = r.Get<std::uint32_t>();
			for (std::uint32_t i = 0; i < numGroups && r.ok; ++i) {
				r.Get<std::uint32_t>();
			}
			if (!r.ok) {
				return "truncated header";
			}
			f.offsets.resize(f.numBlocks);
			std::size_t at = r.pos;
			for (std::uint32_t i = 0; i < f.numBlocks; ++i) {
				f.offsets[i] = at;
				at += f.sizes[i];
			}
			if (at > a_data.size()) {
				return "block sizes run past the end of the file";
			}
			return {};
		}
	}

	std::string ReadNifSkinBoneNames(std::string_view a_data, std::unordered_map<std::string, std::vector<std::string>>& a_out)
	{
		a_out.clear();
		NifFile file;
		if (auto error = ParseNif(a_data, file); !error.empty()) {
			return error;
		}
		const auto& numBlocks = file.numBlocks;
		const auto& sizes = file.sizes;
		const auto& offsets = file.offsets;
		const auto& strings = file.strings;
		auto typeOf = [&](std::int32_t a_block) { return file.TypeOf(a_block); };
		auto nameOf = [&](std::int32_t a_block) -> std::string {  // NiObjectNET::name, the first field
			if (a_block < 0 || static_cast<std::uint32_t>(a_block) >= numBlocks || sizes[a_block] < 4) {
				return {};
			}
			std::uint32_t index;
			std::memcpy(&index, a_data.data() + offsets[a_block], 4);
			return index < strings.size() ? strings[index] : std::string{};
		};

		for (std::uint32_t i = 0; i < numBlocks; ++i) {
			const auto type = typeOf(static_cast<std::int32_t>(i));
			if (type != "BSTriShape"sv && type != "BSSubIndexTriShape"sv && type != "BSDynamicTriShape"sv && type != "BSMeshLODTriShape"sv) {
				continue;
			}
			// NiObjectNET: name, extra data list, controller; NiAVObject: flags, translation, rotation, scale,
			// collision; BSTriShape: bounding sphere, then the skin reference.
			NifReader s{ a_data.substr(offsets[i], sizes[i]) };
			s.Get<std::uint32_t>();
			const auto numExtra = s.Get<std::uint32_t>();
			s.pos += 4ull * numExtra;
			s.pos += 4;            // controller
			s.pos += 4 + 12 + 36 + 4 + 4;  // flags, translation, rotation, scale, collision
			s.pos += 16;           // bounding sphere
			const auto skinRef = s.Get<std::int32_t>();
			if (!s.ok || typeOf(skinRef) != "BSSkin::Instance"sv) {
				continue;
			}
			NifReader k{ a_data.substr(offsets[skinRef], sizes[skinRef]) };
			k.Get<std::int32_t>();  // target
			k.Get<std::int32_t>();  // bone data
			const auto numBones = k.Get<std::uint32_t>();
			if (!k.ok || numBones > 1024) {
				continue;
			}
			std::vector<std::string> names(numBones);
			for (auto& n : names) {
				n = nameOf(k.Get<std::int32_t>());
			}
			if (k.ok) {
				a_out[nameOf(static_cast<std::int32_t>(i))] = std::move(names);
			}
		}
		return a_out.empty() ? "no skinned shapes found" : std::string{};
	}

	std::string ReadNifNodes(std::string_view a_data, std::unordered_map<std::string, NifNode>& a_out)
	{
		a_out.clear();
		NifFile file;
		if (auto error = ParseNif(a_data, file); !error.empty()) {
			return error;
		}
		std::unordered_map<std::int32_t, std::int32_t> parentOf;
		std::unordered_map<std::int32_t, std::string> nameOf;
		std::unordered_map<std::int32_t, RE::NiTransform> localOf;
		for (std::uint32_t i = 0; i < file.numBlocks; ++i) {
			if (file.TypeOf(static_cast<std::int32_t>(i)).find("Node"sv) == std::string_view::npos) {
				continue;
			}
			// NiObjectNET: name, extra data list, controller; NiAVObject (BS version 130): flags, translation,
			// rotation (row-major in the file), scale, collision; NiNode: children.
			NifReader r{ a_data.substr(file.offsets[i], file.sizes[i]) };
			const auto name = r.Get<std::uint32_t>();
			const auto numExtra = r.Get<std::uint32_t>();
			r.pos += 4ull * numExtra + 4 + 4;
			RE::NiTransform t;
			t.MakeIdentity();
			t.translate.x = r.Get<float>();
			t.translate.y = r.Get<float>();
			t.translate.z = r.Get<float>();
			float m[9];
			for (float& v : m) {
				v = r.Get<float>();
			}
			// The engine's loader (1.11.240 +16B92A0) stores the file's matrix transposed.
			for (int row = 0; row < 3; ++row) {
				for (int col = 0; col < 3; ++col) {
					t.rotate.entry[row][col] = m[col * 3 + row];
				}
				t.rotate.entry[row][3] = 0.0f;
			}
			t.scale = r.Get<float>();
			r.Get<std::int32_t>();  // collision
			const auto numChildren = r.Get<std::uint32_t>();
			if (!r.ok || numChildren > 4096) {
				continue;
			}
			for (std::uint32_t c = 0; c < numChildren; ++c) {
				parentOf[r.Get<std::int32_t>()] = static_cast<std::int32_t>(i);
			}
			nameOf[static_cast<std::int32_t>(i)] = name < file.strings.size() ? file.strings[name] : std::string{};
			localOf[static_cast<std::int32_t>(i)] = t;
		}
		for (const auto& [block, name] : nameOf) {
			NifNode node;
			node.local = localOf[block];
			if (const auto it = parentOf.find(block); it != parentOf.end() && nameOf.contains(it->second)) {
				node.parent = nameOf[it->second];
			}
			for (auto b = block; parentOf.contains(b) && node.depth < 256; b = parentOf[b]) {
				++node.depth;
			}
			if (!name.empty()) {
				a_out[name] = std::move(node);
			}
		}
		return a_out.empty() ? "no nodes found" : std::string{};
	}
}
