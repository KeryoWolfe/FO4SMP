#include "Papyrus.h"

#include "Console.h"
#include "PlayerPhysics.h"

// The script functions are written against the virtual machine F4SE hands over, not CommonLibF4's binding templates:
// those find the game's VM through GameVM's singleton (ID 2689134) and a base object name (ID 648543), which aren't in
// Address Library 1.11.240. What this file uses is: NF_util::NativeFunctionBase::Call (2314780), Stack::GetPageForFrame
// (2314680), Stack::GetStackFrameVariable (2314681) and Object::GetHandle (2314431), all present.
//
// The functions aren't callable from tasklets, so the VM runs them on the main thread; PlayerPhysics takes its lock
// anyway.

namespace FO4SMP::Papyrus
{
	namespace
	{
		namespace BS = RE::BSScript;
		using RawType = BS::TypeInfo::RawType;

		BS::IVirtualMachine* g_vm = nullptr;

		class Native : public BS::NF_util::NativeFunctionBase
		{
		public:
			using Args = std::vector<const BS::Variable*>;
			using Body = std::function<void(const Args&, BS::Variable&)>;

			Native(std::string_view a_name, const std::vector<std::pair<std::string, BS::TypeInfo>>& a_params, BS::TypeInfo a_return, Body a_body) :
				NativeFunctionBase("DynamicHDT"sv, a_name, static_cast<std::uint16_t>(a_params.size()), true, false),
				_body(std::move(a_body))
			{
				for (std::size_t i = 0; i < a_params.size(); ++i) {
					descTable.entries[i].first = a_params[i].first;
					descTable.entries[i].second = a_params[i].second;
				}
				retType = a_return;
			}

			bool HasStub() const override { return static_cast<bool>(_body); }

			bool MarshallAndDispatch(BS::Variable&, BS::Internal::VirtualMachine&, std::uint32_t, BS::Variable& a_retVal, const BS::StackFrame& a_frame) const override
			{
				a_retVal = nullptr;
				auto* stack = a_frame.parent;
				if (!stack) {
					return false;
				}
				const auto page = stack->GetPageForFrame(std::addressof(a_frame));
				Args args;
				for (std::uint32_t i = 0; i < descTable.paramCount; ++i) {
					args.push_back(a_frame.size > i ? std::addressof(stack->GetStackFrameVariable(std::addressof(a_frame), i, page)) : nullptr);
				}
				try {
					_body(args, a_retVal);
				} catch (const std::exception& e) {
					logger::error("DynamicHDT.{}: {}", name.c_str(), e.what());
				}
				return true;
			}

		private:
			Body _body;
		};

		// --- arguments ---

		void* FormOf(const BS::Variable* a_var, RE::ENUM_FORM_ID a_type)
		{
			if (!a_var || !g_vm || !a_var->is<BS::Object>()) {
				return nullptr;
			}
			const auto object = BS::get<BS::Object>(*a_var);
			if (!object) {
				return nullptr;
			}
			const auto& handles = g_vm->GetObjectHandlePolicy();
			const auto handle = object->GetHandle();
			if (!handles.IsHandleLoaded(handle)) {
				return nullptr;
			}
			return handles.GetObjectForHandle(static_cast<std::uint32_t>(a_type), handle);
		}

		std::uint32_t ActorID(const BS::Variable* a_var)
		{
			const auto* actor = static_cast<RE::TESForm*>(FormOf(a_var, RE::ENUM_FORM_ID::kACHR));
			return actor ? actor->GetFormID() : 0;
		}

		// An ArmorAddon's third-person models (male, female), relative to Meshes\.
		std::vector<std::string> ArmaModels(const BS::Variable* a_var, std::uint32_t& a_formID)
		{
			std::vector<std::string> out;
			a_formID = 0;
			const auto* arma = static_cast<RE::TESObjectARMA*>(FormOf(a_var, RE::ENUM_FORM_ID::kARMA));
			if (!arma) {
				return out;
			}
			a_formID = arma->GetFormID();
			for (const auto& model : arma->bipedModel) {
				if (const char* path = model.model.c_str(); path && *path) {
					out.emplace_back(path);
				}
			}
			return out;
		}

		std::string String(const BS::Variable* a_var)
		{
			if (!a_var || !a_var->is<RE::BSFixedString>()) {
				return {};
			}
			const auto s = BS::get<RE::BSFixedString>(*a_var);
			return s.c_str() ? std::string{ s.c_str() } : std::string{};
		}

		bool Bool(const BS::Variable* a_var)
		{
			return a_var && a_var->is<bool>() && BS::get<bool>(*a_var);
		}

		std::vector<std::string> Strings(const BS::Variable* a_var)
		{
			std::vector<std::string> out;
			if (!a_var || !a_var->is<BS::Array>()) {
				return out;
			}
			const auto array = BS::get<BS::Array>(*a_var);
			if (!array) {
				return out;
			}
			for (std::uint32_t i = 0; i < array->size(); ++i) {
				out.push_back(String(std::addressof((*array)[i])));
			}
			return out;
		}

		void Say(bool a_verbose, const std::string& a_line)
		{
			logger::info("DynamicHDT: {}", a_line);
			if (a_verbose) {
				Console::Print("[DynamicHDT] -- " + a_line);
			}
		}

		std::string Who(std::uint32_t a_formID)
		{
			return std::format("{:08X}", a_formID);
		}

		// --- registration ---

		bool Register(BS::IVirtualMachine* a_vm)
		{
			g_vm = a_vm;
			auto objectType = [&](const char* a_name) -> BS::TypeInfo {
				RE::BSTSmartPointer<BS::ObjectTypeInfo> info;
				if (!a_vm->GetScriptObjectType(RE::BSFixedString(a_name), info) || !info) {
					logger::error("DynamicHDT: the script type {} isn't known to the VM", a_name);
					return BS::TypeInfo(RawType::kObject);
				}
				static std::vector<RE::BSTSmartPointer<BS::ObjectTypeInfo>> s_keep;
				s_keep.push_back(info);
				return BS::TypeInfo(info.get());
			};
			const auto actor = objectType("Actor");
			const auto armorAddon = objectType("ArmorAddon");
			const BS::TypeInfo string{ RawType::kString };
			const BS::TypeInfo boolean{ RawType::kBool };
			const BS::TypeInfo strings{ RawType::kArrayString };
			const BS::TypeInfo booleans{ RawType::kArrayBool };
			const BS::TypeInfo none{ RawType::kNone };

			int bound = 0;
			auto bind = [&](Native* a_function) {
				a_function->SetCallableFromTasklets(false);
				if (a_vm->BindNativeMethod(a_function)) {
					++bound;
				} else {
					logger::error("DynamicHDT: couldn't bind {}", a_function->GetName().c_str());
				}
			};

			// bool ReloadPhysicsFile(Actor on_actor, ArmorAddon on_ARMA_item, String new_physics_file_path, Bool persist, Bool verbose_log)
			bind(new Native("ReloadPhysicsFile"sv, { { "on_actor", actor }, { "on_ARMA_item", armorAddon }, { "new_physics_file_path", string }, { "persist", boolean }, { "verbose_log", boolean } }, boolean,
				[](const Native::Args& a, BS::Variable& r) {
					const auto id = ActorID(a[0]);
					std::uint32_t arma = 0;
					const auto models = ArmaModels(a[1], arma);
					const auto path = String(a[2]);
					const bool verbose = Bool(a[4]);
					if (!id || !arma || path.empty()) {
						Say(verbose, std::format("ReloadPhysicsFile: couldn't parse parameters (actor {}, ArmorAddon {}, path '{}')", Who(id), Who(arma), path));
						r = false;
						return;
					}
					const auto original = PlayerPhysics::AttachedPhysicsFile(id, models);
					if (original.empty()) {
						Say(verbose, std::format("ReloadPhysicsFile: Character ({}) has no physics from ArmorAddon ({})", Who(id), Who(arma)));
						r = false;
						return;
					}
					const bool ok = PlayerPhysics::SetPhysicsFile(id, original, path, Bool(a[3]));
					Say(verbose, std::format("ReloadPhysicsFile: Character ({}), ArmorAddon ({}): {} -> {}{}", Who(id), Who(arma), original, path, ok ? "" : " failed"));
					r = ok;
				}));

			// bool SwapPhysicsFile(Actor on_actor, String old_physics_file_path, String new_physics_file_path, Bool persist, Bool verbose_log)
			bind(new Native("SwapPhysicsFile"sv, { { "on_actor", actor }, { "old_physics_file_path", string }, { "new_physics_file_path", string }, { "persist", boolean }, { "verbose_log", boolean } }, boolean,
				[](const Native::Args& a, BS::Variable& r) {
					const auto id = ActorID(a[0]);
					const auto from = String(a[1]);
					const auto to = String(a[2]);
					const bool verbose = Bool(a[4]);
					if (!id || from.empty() || to.empty()) {
						Say(verbose, std::format("SwapPhysicsFile: couldn't parse parameters (actor {})", Who(id)));
						r = false;
						return;
					}
					if (!PlayerPhysics::UsesPhysicsFile(id, from)) {
						Say(verbose, std::format("SwapPhysicsFile: Character ({}) doesn't use {}", Who(id), from));
						r = false;
						return;
					}
					// What the NIF names: from itself, or the file that from already replaces.
					const auto original = PlayerPhysics::OriginalPhysicsFile(id, from);
					const bool ok = PlayerPhysics::SetPhysicsFile(id, original, to, Bool(a[3]));
					Say(verbose, std::format("SwapPhysicsFile: Character ({}): {} -> {}{}", Who(id), from, to, ok ? "" : " failed"));
					r = ok;
				}));

			// String QueryCurrentPhysicsFile(Actor on_actor, ArmorAddon on_ARMA_item, Bool verbose_log)
			bind(new Native("QueryCurrentPhysicsFile"sv, { { "on_actor", actor }, { "on_ARMA_item", armorAddon }, { "verbose_log", boolean } }, string,
				[](const Native::Args& a, BS::Variable& r) {
					const auto id = ActorID(a[0]);
					std::uint32_t arma = 0;
					const auto models = ArmaModels(a[1], arma);
					const auto path = id && arma ? PlayerPhysics::CurrentPhysicsFile(id, models) : std::string{};
					Say(Bool(a[2]), std::format("QueryCurrentPhysicsFile: Character ({}), ArmorAddon ({}): '{}'", Who(id), Who(arma), path));
					r = RE::BSFixedString(path.c_str());
				}));

			// Bool HasPhysicsSystem(Actor actor)
			bind(new Native("HasPhysicsSystem"sv, { { "actor", actor } }, boolean,
				[](const Native::Args& a, BS::Variable& r) {
					const auto id = ActorID(a[0]);
					r = id && PlayerPhysics::HasPhysics(id);
				}));

			// Bool IsSoftSuspended(Actor actor): FO4SMP keeps NPCs beyond the simulated ones placed (rest shape, no physics).
			bind(new Native("IsSoftSuspended"sv, { { "actor", actor } }, boolean,
				[](const Native::Args& a, BS::Variable& r) {
					const auto id = ActorID(a[0]);
					r = id && PlayerPhysics::IsPlacedOnly(id);
				}));

			// Bool[] TogglePhysics(Actor actor, String[] boneNames, Bool on)
			bind(new Native("TogglePhysics"sv, { { "actor", actor }, { "boneNames", strings }, { "on", boolean } }, booleans,
				[](const Native::Args& a, BS::Variable& r) {
					const auto id = ActorID(a[0]);
					const auto names = Strings(a[1]);
					const auto previous = id && !names.empty() ? PlayerPhysics::ToggleBones(id, names, Bool(a[2])) : std::vector<bool>(names.size(), false);
					RE::BSTSmartPointer<BS::Array> out;
					if (g_vm && g_vm->CreateArray(BS::TypeInfo(RawType::kArrayBool), static_cast<std::uint32_t>(previous.size()), out) && out) {
						for (std::uint32_t i = 0; i < previous.size(); ++i) {
							out->elements[i] = static_cast<bool>(previous[i]);
						}
						r = std::move(out);
					}
				}));

			// ResetPhysics(Actor actor, Bool full)
			bind(new Native("ResetPhysics"sv, { { "actor", actor }, { "full", boolean } }, none,
				[](const Native::Args& a, BS::Variable&) {
					const auto id = ActorID(a[0]);
					if (id) {
						PlayerPhysics::Reset(id, Bool(a[1]));
					}
				}));

			logger::info("DynamicHDT: {} of 7 script functions bound", bound);
			return true;
		}

		// --- co-save: persisted overrides ---

		constexpr std::uint32_t kUniqueID = 'F4SM';
		constexpr std::uint32_t kOverrides = 'OVRD';
		constexpr std::uint32_t kVersion = 1;

		void WriteString(const F4SE::SerializationInterface* a_intfc, const std::string& a_s)
		{
			const auto n = static_cast<std::uint32_t>(a_s.size());
			a_intfc->WriteRecordData(n);
			a_intfc->WriteRecordData(a_s.data(), n);
		}

		bool ReadString(const F4SE::SerializationInterface* a_intfc, std::string& a_s)
		{
			std::uint32_t n = 0;
			if (a_intfc->ReadRecordData(n) != sizeof(n) || n > 4096) {
				return false;
			}
			a_s.resize(n);
			return a_intfc->ReadRecordData(a_s.data(), n) == n;
		}

		void OnSave(const F4SE::SerializationInterface* a_intfc)
		{
			const auto overrides = PlayerPhysics::PersistedOverrides();
			if (!a_intfc->OpenRecord(kOverrides, kVersion)) {
				logger::error("DynamicHDT: couldn't write the overrides to the co-save");
				return;
			}
			a_intfc->WriteRecordData(static_cast<std::uint32_t>(overrides.size()));
			for (const auto& o : overrides) {
				a_intfc->WriteRecordData(o.formID);
				WriteString(a_intfc, o.original);
				WriteString(a_intfc, o.path);
			}
		}

		void OnLoad(const F4SE::SerializationInterface* a_intfc)
		{
			std::vector<PlayerPhysics::PersistedOverride> overrides;
			std::uint32_t type = 0, version = 0, length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type != kOverrides || version != kVersion) {
					continue;
				}
				std::uint32_t count = 0;
				a_intfc->ReadRecordData(count);
				for (std::uint32_t i = 0; i < count && i < 100000; ++i) {
					PlayerPhysics::PersistedOverride o;
					if (a_intfc->ReadRecordData(o.formID) != sizeof(o.formID) || !ReadString(a_intfc, o.original) || !ReadString(a_intfc, o.path)) {
						logger::error("DynamicHDT: the co-save's override record is cut short");
						break;
					}
					if (const auto resolved = a_intfc->ResolveFormID(o.formID)) {
						o.formID = *resolved;
						overrides.push_back(std::move(o));
					}
				}
			}
			PlayerPhysics::RestoreOverrides(overrides);
		}

		void OnRevert(const F4SE::SerializationInterface*)
		{
			PlayerPhysics::RestoreOverrides({});
		}
	}

	bool Install()
	{
		const auto* papyrus = F4SE::GetPapyrusInterface();
		if (!papyrus || !papyrus->Register(Register)) {
			logger::error("DynamicHDT: F4SE's Papyrus interface isn't available; the script functions aren't registered");
			return false;
		}
		if (const auto* serialization = F4SE::GetSerializationInterface()) {
			serialization->SetUniqueID(kUniqueID);
			serialization->SetSaveCallback(OnSave);
			serialization->SetLoadCallback(OnLoad);
			serialization->SetRevertCallback(OnRevert);
		} else {
			logger::warn("DynamicHDT: F4SE's serialization interface isn't available; persisted overrides won't be saved");
		}
		return true;
	}
}
