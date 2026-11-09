#include "Validation.h"

#include "Validator/Validators/hdtXSDValidator.h"

namespace FO4SMP::Validation
{
	void CheckOnce(const std::string& a_path, const std::string& a_label)
	{
		static std::unordered_set<std::string> s_checked;
		static int s_files = 0;
		std::string key = a_path;
		std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (!s_checked.insert(key).second || s_files >= 500) {
			return;
		}
		++s_files;
		try {
			const auto result = hdt::ValidatePhysicsXMLWithXSD(a_path);
			if (result.schemaSkipped) {
				static bool s_said = false;
				if (!std::exchange(s_said, true)) {
					logger::warn("Validator: no schema (F4SE\\Plugins\\FO4SMP\\hdtSMP64.xsd); physics XMLs aren't checked");
				}
				return;
			}
			if (result.violations.empty()) {
				logger::info("Validator: {} passed", a_label);
				return;
			}
			logger::warn("Validator: {} has {} problem(s){}", a_label, result.violations.size(), result.violations.size() > 20 ? " (first 20)" : "");
			int shown = 0;
			for (const auto& v : result.violations) {
				if (++shown > 20) {
					break;
				}
				logger::warn("  line {}{}: {}", v.line, v.elementPath.empty() ? std::string{} : std::format(" ({})", v.elementPath), v.message);
			}
		} catch (const std::exception& e) {
			logger::warn("Validator: {} couldn't be checked: {}", a_label, e.what());
		}
	}
}
