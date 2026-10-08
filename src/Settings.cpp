#include "Settings.h"

#include <SimpleIni.h>

namespace mis
{
	namespace
	{
		// The reader ReadSettings (generated from the settings sheet) expects.
		struct IniReader
		{
			const CSimpleIniA& ini;

			bool GetBool(const char* a_section, const char* a_key, bool a_default) const { return ini.GetBoolValue(a_section, a_key, a_default); }
			std::int32_t GetInt(const char* a_section, const char* a_key, std::int32_t a_default) const { return static_cast<std::int32_t>(ini.GetLongValue(a_section, a_key, a_default)); }
			float GetFloat(const char* a_section, const char* a_key, float a_default) const { return static_cast<float>(ini.GetDoubleValue(a_section, a_key, a_default)); }
			std::string GetString(const char* a_section, const char* a_key, const std::string& a_default) const { return ini.GetValue(a_section, a_key, a_default.c_str()); }
			sheets::Pool GetPool(const char* a_section, const char* a_key, sheets::Pool a_default) const
			{
				const char* v = ini.GetValue(a_section, a_key, nullptr);
				if (!v) {
					return a_default;
				}
				if (auto p = PoolByName(v)) {
					return *p;
				}
				logger::warn("[{}] {} = {} is not a music pool; using the default", a_section, a_key, v);
				return a_default;
			}
		};

		bool LooksLikeFnv(const fs::path& a_dir)
		{
			std::error_code ec;
			return !a_dir.empty() && fs::exists(a_dir / sheets::kSource_songs.path, ec);
		}

		std::optional<std::wstring> RegString(HKEY a_root, const wchar_t* a_key, const wchar_t* a_value)
		{
			wchar_t buf[1024];
			DWORD size = sizeof(buf);
			if (::RegGetValueW(a_root, a_key, a_value, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) {
				return std::wstring(buf);
			}
			return std::nullopt;
		}
	}

	std::optional<sheets::Pool> PoolByName(std::string_view a_name)
	{
		for (const auto& p : sheets::kPools) {
			if (p.name.size() == a_name.size() && _strnicmp(p.name.data(), a_name.data(), a_name.size()) == 0) {
				return p.id;
			}
		}
		return std::nullopt;
	}

	void Settings::Load(const fs::path& a_ini)
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(a_ini.c_str()) < 0) {
			logger::info("settings: {} not found, using defaults", a_ini.string());
		}
		sheets::ReadSettings(IniReader{ ini }, values);
		if (values.debug_bVerboseLog) {
			spdlog::default_logger()->set_level(spdlog::level::debug);
			spdlog::default_logger()->flush_on(spdlog::level::debug);
		}
		logger::info("settings: radio key {:#x}, radio volume {}, music volume {}, crossfade {}s, day {}h-{}h",
			values.radio_iToggleKey, values.radio_fVolume, values.music_fVolume, values.music_fCrossfadeSeconds,
			values.music_iDayStartHour, values.music_iNightStartHour);
	}

	std::optional<fs::path> Settings::FalloutNVFolder()
	{
		std::vector<std::pair<const char*, fs::path>> candidates;
		if (!values.paths_sFalloutNVPath.empty()) {
			candidates.emplace_back("the INI", fs::u8path(values.paths_sFalloutNVPath));
		}
		if (wchar_t buf[2048]; ::GetEnvironmentVariableW(L"MOJAVEINSKYRIM_FNV", buf, 2048) > 0) {
			candidates.emplace_back("Melty", fs::path(buf));
		}
		if (auto p = RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Bethesda Softworks\\FalloutNV", L"Installed Path")) {
			candidates.emplace_back("the New Vegas registry entry", fs::path(*p));
		}
		if (auto steam = RegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath")) {
			std::vector<fs::path> libs{ fs::path(*steam) };
			std::ifstream vdf(fs::path(*steam) / "steamapps" / "libraryfolders.vdf");
			for (std::string line; std::getline(vdf, line);) {
				const auto key = line.find("\"path\"");
				if (key == std::string::npos) {
					continue;
				}
				const auto a = line.find('"', key + 6);
				const auto b = a == std::string::npos ? a : line.find('"', a + 1);
				if (b != std::string::npos) {
					std::string p = line.substr(a + 1, b - a - 1);
					for (std::size_t i; (i = p.find("\\\\")) != std::string::npos;) {
						p.erase(i, 1);
					}
					libs.emplace_back(fs::u8path(p));
				}
			}
			for (const auto& lib : libs) {
				candidates.emplace_back("Steam", lib / "steamapps" / "common" / "Fallout New Vegas");
			}
		}
		for (const auto& [from, dir] : candidates) {
			if (LooksLikeFnv(dir)) {
				logger::info("Fallout: New Vegas found through {}: {}", from, dir.string());
				return dir;
			}
			logger::info("not New Vegas ({}): {}", from, dir.string());
		}
		return std::nullopt;
	}
}
