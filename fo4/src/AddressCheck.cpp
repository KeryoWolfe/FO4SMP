#include "AddressCheck.h"

#include "Offsets.h"

namespace FO4SMP::AddressCheck
{
	namespace
	{
		// Resolves an Address Library ID without throwing; 0 when the ID is missing
		// from the database for this runtime.
		std::uintptr_t Resolve(const REL::ID& a_id) noexcept
		{
			try {
				return a_id.address();
			} catch (...) {
				return 0;
			}
		}

		// Compares the first bytes at a_address with a_expected. 0x100 in a_expected
		// is a wildcard.
		bool BytesMatch(std::uintptr_t a_address, std::span<const std::uint16_t> a_expected) noexcept
		{
			const auto* code = reinterpret_cast<const std::uint8_t*>(a_address);
			for (std::size_t i = 0; i < a_expected.size(); ++i) {
				if (a_expected[i] != 0x100 && code[i] != a_expected[i]) {
					return false;
				}
			}
			return true;
		}

		std::uintptr_t Base() noexcept { return REL::Module::get().base(); }

		bool Report(std::string_view a_name, const REL::ID& a_id, std::uintptr_t a_address, bool a_ok, std::string_view a_what)
		{
			if (a_address == 0) {
				logger::error("Address check: {} (ID {}) FAILED: not in Address Library for this runtime", a_name, a_id.id());
				return false;
			}
			if (a_ok) {
				logger::info("Address check: {} (ID {}) at +{:07X} OK: {}", a_name, a_id.id(), a_address - Base(), a_what);
			} else {
				logger::error("Address check: {} (ID {}) at +{:07X} FAILED: {}", a_name, a_id.id(), a_address - Base(), a_what);
			}
			return a_ok;
		}
	}

	bool Run()
	{
		bool all = true;

		// 1. BSFaceGenNiNode's vtable: its RTTI locator (vtable[-1]) must name BSFaceGenNiNode.
		const auto vtbl = Resolve(Offsets::BSFaceGenNiNode_vtbl);
		{
			bool ok = false;
			if (vtbl != 0) {
				// MSVC x64: vtable[-1] -> CompleteObjectLocator; +0x0C = TypeDescriptor RVA; TypeDescriptor+0x10 = mangled name
				const auto col = *reinterpret_cast<const std::uintptr_t*>(vtbl - sizeof(void*));
				const auto tdRVA = *reinterpret_cast<const std::uint32_t*>(col + 0x0C);
				const auto* name = reinterpret_cast<const char*>(Base() + tdRVA + 0x10);
				ok = std::string_view{ name } == ".?AVBSFaceGenNiNode@@"sv;
			}
			all &= Report("BSFaceGenNiNode vtable", Offsets::BSFaceGenNiNode_vtbl, vtbl, ok, "RTTI name is BSFaceGenNiNode");
		}

		// 2. SkinAllGeometry: must be the function in vtable slot 0x43, and start with test rdx,rdx / je.
		{
			const auto fn = Resolve(Offsets::BSFaceGenNiNode_SkinAllGeometry);
			bool ok = false;
			if (fn != 0 && vtbl != 0) {
				const auto slot = reinterpret_cast<const std::uintptr_t*>(vtbl)[Offsets::BSFaceGenNiNode_SkinAllGeometry_slot];
				static constexpr std::array<std::uint16_t, 5> head{ 0x48, 0x85, 0xD2, 0x0F, 0x84 };
				ok = slot == fn && BytesMatch(fn, head);
			}
			all &= Report("BSFaceGenNiNode::SkinAllGeometry", Offsets::BSFaceGenNiNode_SkinAllGeometry, fn, ok, "is vtable slot 0x43 and its code starts as expected");
		}

		// 3. TESNPC::GetFaceGeomPath: prologue and the cmp [rcx+0x270], 0 it starts with.
		{
			const auto fn = Resolve(Offsets::TESNPC_GetFaceGeomPath);
			static constexpr std::array<std::uint16_t, 23> head{
				0x48, 0x89, 0x5C, 0x24, 0x08,              // mov [rsp+8], rbx
				0x48, 0x89, 0x74, 0x24, 0x10,              // mov [rsp+10h], rsi
				0x57,                                      // push rdi
				0x48, 0x83, 0xEC, 0x30,                    // sub rsp, 30h
				0x48, 0x83, 0xB9, 0x70, 0x02, 0x00, 0x00, 0x00  // cmp qword ptr [rcx+270h], 0
			};
			const bool ok = fn != 0 && BytesMatch(fn, head);
			all &= Report("TESNPC::GetFaceGeomPath", Offsets::TESNPC_GetFaceGeomPath, fn, ok, "code starts as expected");
		}

		return all;
	}
}
