#include "AddressCheck.h"

#include "Offsets.h"

namespace FO4SMP::AddressCheck
{
	namespace
	{
		enum class Outcome
		{
			kOK,
			kHooked,
			kFailed
		};

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

		std::uintptr_t Base() noexcept { return REL::Module::get().base(); }

		// The file name of the module that owns a_address ("" if none).
		std::string ModuleOf(std::uintptr_t a_address)
		{
			REX::W32::MEMORY_BASIC_INFORMATION info{};
			if (REX::W32::VirtualQuery(reinterpret_cast<const void*>(a_address), &info, sizeof(info)) == 0 || !info.allocationBase) {
				return {};
			}
			char path[260]{};
			const auto len = REX::W32::GetModuleFileNameA(reinterpret_cast<REX::W32::HMODULE>(info.allocationBase), path, static_cast<std::uint32_t>(std::size(path)));
			if (len == 0) {
				return {};
			}
			return std::filesystem::path{ std::string_view{ path, len } }.filename().string();
		}

		// Where a jump at a_address leads, if the code there starts with one (E9 rel32, FF 25 [rip+disp32],
		// or mov rax, imm64 / jmp rax), as a detour or trampoline hook leaves it. 0 when it doesn't.
		std::uintptr_t JumpTarget(std::uintptr_t a_address) noexcept
		{
			const auto* code = reinterpret_cast<const std::uint8_t*>(a_address);
			if (code[0] == 0xE9) {
				std::int32_t rel;
				std::memcpy(&rel, code + 1, sizeof(rel));
				return a_address + 5 + rel;
			}
			if (code[0] == 0xFF && code[1] == 0x25) {
				std::int32_t disp;
				std::memcpy(&disp, code + 2, sizeof(disp));
				std::uintptr_t target;
				std::memcpy(&target, reinterpret_cast<const void*>(a_address + 6 + disp), sizeof(target));
				return target;
			}
			if (code[0] == 0x48 && code[1] == 0xB8 && code[10] == 0xFF && code[11] == 0xE0) {
				std::uintptr_t target;
				std::memcpy(&target, code + 2, sizeof(target));
				return target;
			}
			return 0;
		}

		// Follows a hook's jumps (a trampoline often jumps again) until one lands in a module.
		// Returns that module's file name, or "" when the chain ends outside any module.
		std::string HookOwner(std::uintptr_t a_target)
		{
			for (int hop = 0; hop < 4 && a_target != 0; ++hop) {
				if (auto owner = ModuleOf(a_target); !owner.empty()) {
					return owner;
				}
				a_target = JumpTarget(a_target);
			}
			return {};
		}

		std::string Hex(std::uintptr_t a_address, std::size_t a_count)
		{
			const auto* code = reinterpret_cast<const std::uint8_t*>(a_address);
			std::string out;
			for (std::size_t i = 0; i < a_count; ++i) {
				out += std::format("{}{:02X}", i ? " " : "", code[i]);
			}
			return out;
		}

		// Compares the code at a_address with a_expected (0x100 = any byte). A mismatch that is a jump
		// into another module is reported as hooked: the address is right, but someone patched it.
		Outcome CheckCode(std::string_view a_name, const REL::ID& a_id, std::uintptr_t a_address, std::span<const std::uint16_t> a_expected, std::string_view a_note = {})
		{
			if (a_address == 0) {
				logger::error("Address check: {} (ID {}) FAILED: not in Address Library for this runtime", a_name, a_id.id());
				return Outcome::kFailed;
			}
			const auto* code = reinterpret_cast<const std::uint8_t*>(a_address);
			bool match = true;
			for (std::size_t i = 0; i < a_expected.size(); ++i) {
				if (a_expected[i] != 0x100 && code[i] != a_expected[i]) {
					match = false;
					break;
				}
			}
			if (match) {
				logger::info("Address check: {} (ID {}) at +{:07X} OK: code starts as expected{}", a_name, a_id.id(), a_address - Base(), a_note);
				return Outcome::kOK;
			}
			if (const auto target = JumpTarget(a_address)) {
				const auto owner = HookOwner(target);
				logger::warn("Address check: {} (ID {}) at +{:07X} HOOKED by {}: starts with a jump, so its code can't be compared. Check again with that plugin off.",
					a_name, a_id.id(), a_address - Base(), owner.empty() ? "another plugin"s : owner);
				return Outcome::kHooked;
			}
			logger::error("Address check: {} (ID {}) at +{:07X} FAILED: code starts {} (expected {} bytes from 1.11.240)",
				a_name, a_id.id(), a_address - Base(), Hex(a_address, a_expected.size()), a_expected.size());
			return Outcome::kFailed;
		}

		void Count(Result& a_result, Outcome a_outcome)
		{
			switch (a_outcome) {
			case Outcome::kOK:
				++a_result.ok;
				break;
			case Outcome::kHooked:
				++a_result.hooked;
				break;
			case Outcome::kFailed:
				++a_result.failed;
				break;
			}
		}
	}

	Result Run()
	{
		Result result;

		// 1. BSFaceGenNiNode's vtable: its RTTI locator (vtable[-1]) must name BSFaceGenNiNode.
		const auto vtbl = Resolve(Offsets::BSFaceGenNiNode_vtbl);
		{
			if (vtbl == 0) {
				logger::error("Address check: BSFaceGenNiNode vtable (ID {}) FAILED: not in Address Library for this runtime", Offsets::BSFaceGenNiNode_vtbl.id());
				Count(result, Outcome::kFailed);
			} else {
				// MSVC x64: vtable[-1] -> CompleteObjectLocator; +0x0C = TypeDescriptor RVA; TypeDescriptor+0x10 = mangled name
				const auto col = *reinterpret_cast<const std::uintptr_t*>(vtbl - sizeof(void*));
				const auto tdRVA = *reinterpret_cast<const std::uint32_t*>(col + 0x0C);
				const auto* name = reinterpret_cast<const char*>(Base() + tdRVA + 0x10);
				const bool ok = std::string_view{ name } == ".?AVBSFaceGenNiNode@@"sv;
				if (ok) {
					logger::info("Address check: BSFaceGenNiNode vtable (ID {}) at +{:07X} OK: RTTI name is BSFaceGenNiNode", Offsets::BSFaceGenNiNode_vtbl.id(), vtbl - Base());
				} else {
					logger::error("Address check: BSFaceGenNiNode vtable (ID {}) at +{:07X} FAILED: RTTI name is {}", Offsets::BSFaceGenNiNode_vtbl.id(), vtbl - Base(), name);
				}
				Count(result, ok ? Outcome::kOK : Outcome::kFailed);
			}
		}

		// 2. SkinAllGeometry: vtable slot 0x43 must point at it (or, if another plugin replaced the slot,
		//    somewhere in that plugin), and its code must start with test rdx,rdx / je.
		{
			const auto fn = Resolve(Offsets::BSFaceGenNiNode_SkinAllGeometry);
			static constexpr std::array<std::uint16_t, 5> head{ 0x48, 0x85, 0xD2, 0x0F, 0x84 };
			auto outcome = CheckCode("BSFaceGenNiNode::SkinAllGeometry"sv, Offsets::BSFaceGenNiNode_SkinAllGeometry, fn, head);
			if (vtbl != 0 && fn != 0) {
				const auto slot = reinterpret_cast<const std::uintptr_t*>(vtbl)[Offsets::BSFaceGenNiNode_SkinAllGeometry_slot];
				if (slot == fn) {
					logger::info("Address check: BSFaceGenNiNode vtable slot 0x43 points at SkinAllGeometry");
				} else if (const auto owner = ModuleOf(slot); !owner.empty() && owner != ModuleOf(fn)) {
					logger::warn("Address check: BSFaceGenNiNode vtable slot 0x43 HOOKED by {}: it points into that module, not at SkinAllGeometry", owner);
					if (outcome == Outcome::kOK) {
						outcome = Outcome::kHooked;
					}
				} else {
					logger::error("Address check: BSFaceGenNiNode vtable slot 0x43 FAILED: points at +{:07X} ({}), not SkinAllGeometry", slot - Base(), owner);
					outcome = Outcome::kFailed;
				}
			}
			Count(result, outcome);
		}

		// 3. TESNPC::GetFaceGeomPath: prologue and the cmp [rcx+0x270], 0 it starts with.
		{
			static constexpr std::array<std::uint16_t, 23> head{
				0x48, 0x89, 0x5C, 0x24, 0x08,                    // mov [rsp+8], rbx
				0x48, 0x89, 0x74, 0x24, 0x10,                    // mov [rsp+10h], rsi
				0x57,                                            // push rdi
				0x48, 0x83, 0xEC, 0x30,                          // sub rsp, 30h
				0x48, 0x83, 0xB9, 0x70, 0x02, 0x00, 0x00, 0x00  // cmp qword ptr [rcx+270h], 0
			};
			Count(result, CheckCode("TESNPC::GetFaceGeomPath"sv, Offsets::TESNPC_GetFaceGeomPath, Resolve(Offsets::TESNPC_GetFaceGeomPath), head));
		}

		// 4. The console log's singleton pointer: the ID must resolve (the pointer itself is set later, when the console exists).
		{
			const auto address = Resolve(Offsets::ConsoleLog_Singleton);
			if (address != 0) {
				logger::info("Address check: ConsoleLog singleton (ID {}) at +{:07X} OK: resolves (data)", Offsets::ConsoleLog_Singleton.id(), address - Base());
			} else {
				logger::error("Address check: ConsoleLog singleton (ID {}) FAILED: not in Address Library for this runtime", Offsets::ConsoleLog_Singleton.id());
			}
			Count(result, address != 0 ? Outcome::kOK : Outcome::kFailed);
		}

		// 5. The function table: each ID must resolve and its code start with the bytes seen in 1.11.240.
		for (const auto& fn : Offsets::kFunctions) {
			const REL::ID id{ fn.id };
			std::array<std::uint16_t, 8> head{};
			std::ranges::copy(fn.head, head.begin());
			Count(result, CheckCode(fn.name, id, Resolve(id), head, fn.strong ? ""sv : " (weak match)"sv));
		}

		logger::info("Address check: {} OK, {} hooked by another plugin, {} failed", result.ok, result.hooked, result.failed);
		return result;
	}
}
