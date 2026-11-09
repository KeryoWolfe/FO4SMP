#include "Settings.h"

#include "PlayerPhysics.h"

namespace FO4SMP::Settings
{
	namespace
	{
		constexpr auto kPath = "Data/MCM/Settings/FO4SMP.ini";

		std::string Trim(std::string_view a_s)
		{
			const auto first = a_s.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_s.find_last_not_of(" \t\r\n");
			return std::string{ a_s.substr(first, last - first + 1) };
		}

		// "section:key" (lower case) -> value
		std::map<std::string, std::string> ReadIni(const std::filesystem::path& a_path)
		{
			std::map<std::string, std::string> out;
			std::ifstream file(a_path);
			std::string line, section;
			while (std::getline(file, line)) {
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#') {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = Trim(std::string_view{ line }.substr(1, line.size() - 2));
					std::ranges::transform(section, section.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}
				auto key = Trim(std::string_view{ line }.substr(0, eq));
				std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				out[section + ":" + key] = Trim(std::string_view{ line }.substr(eq + 1));
			}
			return out;
		}

		void Load(const std::filesystem::path& a_path)
		{
			const auto ini = ReadIni(a_path);
			PlayerPhysics::Settings settings;
			auto number = [&](const char* a_key) -> std::optional<double> {
				const auto it = ini.find(a_key);
				if (it == ini.end()) {
					return std::nullopt;
				}
				try {
					return std::stod(it->second);
				} catch (...) {
					logger::warn("Settings: {} = '{}' isn't a number; ignored", a_key, it->second);
					return std::nullopt;
				}
			};
			if (const auto v = number("main:bnpcphysics")) {
				settings.npcPhysics = *v != 0.0;
			}
			if (const auto v = number("main:bfacephysics")) {
				settings.facePhysics = *v != 0.0;
			}
			if (const auto v = number("main:bcullnpcs")) {
				settings.cullOffscreen = *v != 0.0;
			}
			if (const auto v = number("main:bverboselog")) {
				const bool verbose = *v != 0.0;
				if (verbose != logger::Verbose()) {
					logger::g_verbose = verbose;
					if (auto log = spdlog::default_logger()) {
						log->set_level(verbose ? spdlog::level::debug : spdlog::level::info);
						log->flush_on(verbose ? spdlog::level::debug : spdlog::level::info);
					}
					logger::info("Settings: verbose log {}", verbose ? "on (debug lines and diagnostics)" : "off");
				}
			}
			if (const auto v = number("main:imaxnpcs")) {
				settings.maxNpcs = static_cast<int>(std::lround(*v));
			}
			if (const auto v = number("main:fnpcdistance")) {
				settings.npcDistance = static_cast<float>(*v);
			}
			logger::info("Settings: read {} value(s) from {}", ini.size(), a_path.string());
			PlayerPhysics::ApplySettings(settings);
		}
	}

	void Poll()
	{
		static auto s_next = std::chrono::steady_clock::time_point{};
		static std::filesystem::file_time_type s_seen{};
		static bool s_had = false;
		const auto now = std::chrono::steady_clock::now();
		if (now < s_next) {
			return;
		}
		s_next = now + std::chrono::seconds(2);
		std::error_code error;
		const std::filesystem::path path{ kPath };
		const auto time = std::filesystem::last_write_time(path, error);
		if (error) {
			if (s_had) {
				logger::info("Settings: {} is gone; the values set stay until the game restarts", kPath);
				s_had = false;
			}
			return;
		}
		if (s_had && time == s_seen) {
			return;
		}
		s_had = true;
		s_seen = time;
		Load(path);
	}
}
