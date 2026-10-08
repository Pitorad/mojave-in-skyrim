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

		// Radio loudness as the player set it in game (volume keys); starts from the INI, then from
		// MojaveInSkyrim.user.ini next to the log if the player has changed it before.
		static float RadioVolume() { return radioVolume; }
		// Adds a_delta (clamped to the INI's range), saves it to the user file, returns the new value.
		static float NudgeRadioVolume(float a_delta);

		// The Fallout: New Vegas folder: the INI's sFalloutNVPath, else MOJAVEINSKYRIM_FNV (Melty passes
		// {game:fallout-new-vegas} there), else the install Steam or the New Vegas installer registered.
		static std::optional<fs::path> FalloutNVFolder();

	private:
		static inline sheets::Settings values{};
		static inline std::atomic<float> radioVolume{ 1.0f };
	};

	std::optional<sheets::Pool> PoolByName(std::string_view a_name);
}
