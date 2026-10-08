#pragma once

#include "generated/Sheets.h"

namespace mis
{
	class Settings
	{
	public:
		// Reads the INI next to the .asi (every settings sheet row; missing lines keep defaults).
		static void Load(const fs::path& a_ini);
		static const sheets::Settings& Get() { return values; }

		// The Fallout: New Vegas folder: the INI's sFalloutNVPath, else MOJAVEINSKYRIM_FNV (Melty passes
		// {game:fallout-new-vegas} there), else the install Steam or the New Vegas installer registered.
		static std::optional<fs::path> FalloutNVFolder();

	private:
		static inline sheets::Settings values{};
	};

	std::optional<sheets::Pool> PoolByName(std::string_view a_name);
}
