#pragma once

// Fallout 4 stand-ins for FSMP's NetImmerseUtils.h: finding nodes and shapes, reading files, and
// recomputing world transforms. Types are told apart by the game's own MSVC RTTI (the class
// hierarchy behind each object's vtable), not by CommonLibF4's RTTI IDs or virtual calls, so they
// don't depend on Address Library IDs or vtable slots that differ between runtimes.

namespace hdt::fo4
{
	inline constexpr std::uintptr_t kCanonicalUserSpaceMax = 0x00007FFFFFFFFFFFull;

	// The module (image base) a vtable belongs to: RTTI RVAs are relative to it.
	inline std::uintptr_t ImageBaseOf(const void* a_address) noexcept
	{
		REX::W32::MEMORY_BASIC_INFORMATION info{};
		if (REX::W32::VirtualQuery(a_address, &info, sizeof(info)) == 0) {
			return 0;
		}
		return reinterpret_cast<std::uintptr_t>(info.allocationBase);
	}

	inline bool IsKindOfUncached(std::uintptr_t a_vtbl, std::string_view a_mangledName) noexcept;

	// True if a_object's class is a_mangledName (".?AVNiNode@@") or derives from it. Answers are cached
	// per vtable: the RTTI walk (and its VirtualQuery) is too slow to repeat for every node of every
	// skeleton search.
	inline bool IsKindOf(const void* a_object, std::string_view a_mangledName) noexcept
	{
		const auto object = reinterpret_cast<std::uintptr_t>(a_object);
		if (!object || object > kCanonicalUserSpaceMax) {
			return false;
		}
		const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(object);
		if (!vtbl || vtbl > kCanonicalUserSpaceMax) {
			return false;
		}
		struct KeyHash
		{
			std::size_t operator()(const std::pair<std::uintptr_t, std::size_t>& a_key) const noexcept { return a_key.first * 31 ^ a_key.second; }
		};
		static std::mutex lock;
		static std::unordered_map<std::pair<std::uintptr_t, std::size_t>, bool, KeyHash> cache;
		const std::pair key{ vtbl, std::hash<std::string_view>{}(a_mangledName) };
		{
			std::scoped_lock guard{ lock };
			if (const auto it = cache.find(key); it != cache.end()) {
				return it->second;
			}
		}
		const bool result = IsKindOfUncached(vtbl, a_mangledName);
		std::scoped_lock guard{ lock };
		cache.emplace(key, result);
		return result;
	}

	inline bool IsKindOfUncached(std::uintptr_t vtbl, std::string_view a_mangledName) noexcept
	{
		const auto col = *reinterpret_cast<const std::uintptr_t*>(vtbl - sizeof(void*));
		if (!col || col > kCanonicalUserSpaceMax) {
			return false;
		}
		const auto base = ImageBaseOf(reinterpret_cast<const void*>(vtbl));
		if (!base || *reinterpret_cast<const std::uint32_t*>(col) != 1) {  // x64 locator signature
			return false;
		}
		// CompleteObjectLocator: +0x10 class hierarchy descriptor RVA
		const auto chd = base + *reinterpret_cast<const std::uint32_t*>(col + 0x10);
		const auto numBases = *reinterpret_cast<const std::uint32_t*>(chd + 0x08);
		const auto baseArray = base + *reinterpret_cast<const std::uint32_t*>(chd + 0x0C);
		for (std::uint32_t i = 0; i < numBases && i < 64; ++i) {
			const auto bcd = base + reinterpret_cast<const std::uint32_t*>(baseArray)[i];
			const auto td = base + *reinterpret_cast<const std::uint32_t*>(bcd);
			if (std::string_view{ reinterpret_cast<const char*>(td + 0x10) } == a_mangledName) {
				return true;
			}
		}
		return false;
	}

	// The object's own class name, for logs ("BSSubIndexTriShape").
	inline std::string ClassName(const void* a_object)
	{
		const auto object = reinterpret_cast<std::uintptr_t>(a_object);
		if (!object || object > kCanonicalUserSpaceMax) {
			return "null";
		}
		const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(object);
		const auto col = *reinterpret_cast<const std::uintptr_t*>(vtbl - sizeof(void*));
		const auto base = ImageBaseOf(reinterpret_cast<const void*>(vtbl));
		if (!base) {
			return "?";
		}
		std::string_view name{ reinterpret_cast<const char*>(base + *reinterpret_cast<const std::uint32_t*>(col + 0x0C) + 0x10) };
		if (name.starts_with(".?AV")) {
			name.remove_prefix(4);
		}
		if (name.ends_with("@@")) {
			name.remove_suffix(2);
		}
		return std::string{ name };
	}

	inline RE::NiNode* castNiNode(RE::NiAVObject* a_object) noexcept
	{
		return IsKindOf(a_object, ".?AVNiNode@@"sv) ? static_cast<RE::NiNode*>(a_object) : nullptr;
	}

	inline RE::BSTriShape* castBSTriShape(RE::NiAVObject* a_object) noexcept
	{
		return IsKindOf(a_object, ".?AVBSTriShape@@"sv) ? static_cast<RE::BSTriShape*>(a_object) : nullptr;
	}

	inline RE::NiTObjectArray<RE::NiPointer<RE::NiAVObject>>& Children(RE::NiNode* a_node) noexcept
	{
		return a_node->GetRuntimeData().children;
	}

	// Depth-first search by name (BSFixedString compares pooled pointers, case-insensitively).
	inline RE::NiAVObject* findObject(RE::NiAVObject* a_root, const RE::BSFixedString& a_name)
	{
		if (!a_root) {
			return nullptr;
		}
		if (a_root->name == a_name) {
			return a_root;
		}
		if (auto* node = castNiNode(a_root)) {
			for (auto& child : Children(node)) {
				if (auto* found = findObject(child.get(), a_name)) {
					return found;
				}
			}
		}
		return nullptr;
	}

	inline RE::NiNode* findNode(RE::NiNode* a_root, const RE::BSFixedString& a_name)
	{
		return castNiNode(findObject(a_root, a_name));
	}

	// The value of a_object's NiStringExtraData named a_key, or "".
	inline std::string_view StringExtraData(RE::NiAVObject* a_object, std::string_view a_key)
	{
		if (!a_object || !a_object->extra) {
			return {};
		}
		for (auto* data : *a_object->extra) {
			if (data && IsKindOf(data, ".?AVNiStringExtraData@@"sv) && std::string_view{ data->name.c_str() } == a_key) {
				return static_cast<RE::NiStringExtraData*>(data)->data.c_str();
			}
		}
		return {};
	}

	// Reads a file the game would see, relative to Data (MO2's virtual folder included).
	// Loose files only for now: a file packed in a BA2 isn't found (the game's resource streams come later).
	inline std::string readAllFile(const char* a_path)
	{
		std::filesystem::path path{ a_path };
		if (path.is_relative()) {
			path = std::filesystem::path{ "Data" } / path;
		}
		std::ifstream stream(path, std::ios::binary);
		if (!stream) {
			return {};
		}
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		return buffer.str();
	}

	// --- transforms (Fallout 4's NiMatrix3 rows are NiPoint4, padded) ---

	inline RE::NiMatrix3 Mul(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
	{
		RE::NiMatrix3 r;
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				r.entry[i][j] = a.entry[i][0] * b.entry[0][j] + a.entry[i][1] * b.entry[1][j] + a.entry[i][2] * b.entry[2][j];
			}
			r.entry[i][3] = 0.0f;
		}
		return r;
	}

	inline RE::NiPoint3 Mul(const RE::NiMatrix3& m, const RE::NiPoint3& v)
	{
		return { m.entry[0][0] * v.x + m.entry[0][1] * v.y + m.entry[0][2] * v.z,
			m.entry[1][0] * v.x + m.entry[1][1] * v.y + m.entry[1][2] * v.z,
			m.entry[2][0] * v.x + m.entry[2][1] * v.y + m.entry[2][2] * v.z };
	}

	inline RE::NiMatrix3 Transposed(const RE::NiMatrix3& m)
	{
		RE::NiMatrix3 r;
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				r.entry[i][j] = m.entry[j][i];
			}
			r.entry[i][3] = 0.0f;
		}
		return r;
	}

	// Fallout 4 stores rotations transposed (row-vector convention): the engine's NiTransform multiply
	// (1.11.240 +0344820) computes world.rotate = local.rotate * parent.rotate and
	// world.translate = parent.translate + parent.scale * (local.translate as a row vector * parent.rotate).
	inline RE::NiTransform Compose(const RE::NiTransform& a_parent, const RE::NiTransform& a_local)
	{
		RE::NiTransform r;
		r.rotate = Mul(a_local.rotate, a_parent.rotate);
		const auto t = Mul(Transposed(a_parent.rotate), a_local.translate);  // row vector * parent.rotate
		r.translate = { a_parent.translate.x + t.x * a_parent.scale, a_parent.translate.y + t.y * a_parent.scale, a_parent.translate.z + t.z * a_parent.scale };
		r.scale = a_parent.scale * a_local.scale;
		return r;
	}

	// The local transform that puts a child at a_world under a_parent (inverse of Compose; rotations are
	// orthonormal).
	inline RE::NiTransform LocalFromWorld(const RE::NiTransform& a_parent, const RE::NiTransform& a_world)
	{
		RE::NiTransform r;
		const float invScale = a_parent.scale != 0.0f ? 1.0f / a_parent.scale : 1.0f;
		r.rotate = Mul(a_world.rotate, Transposed(a_parent.rotate));
		const RE::NiPoint3 d{ a_world.translate.x - a_parent.translate.x, a_world.translate.y - a_parent.translate.y, a_world.translate.z - a_parent.translate.z };
		const auto t = Mul(a_parent.rotate, d);  // row vector * parent.rotate^T
		r.translate = { t.x * invScale, t.y * invScale, t.z * invScale };
		r.scale = a_world.scale * invScale;
		return r;
	}

	// Recomputes world transforms below a_object from their local transforms (FSMP's updateTransformUpDown,
	// done in plain math instead of the engine's virtual update).
	inline void updateTransformUpDown(RE::NiAVObject* a_object, bool /*a_dirty*/)
	{
		if (!a_object) {
			return;
		}
		if (a_object->parent) {
			a_object->world = Compose(a_object->parent->world, a_object->local);
		}
		if (auto* node = castNiNode(a_object)) {
			for (auto& child : Children(node)) {
				if (child) {
					updateTransformUpDown(child.get(), false);
				}
			}
		}
	}
}

namespace hdt
{
	using fo4::castBSTriShape;
	using fo4::castNiNode;
	using fo4::findNode;
	using fo4::findObject;
	using fo4::readAllFile;
	using fo4::updateTransformUpDown;
}
