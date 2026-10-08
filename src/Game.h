#pragma once

#include "generated/Sheets.h"

namespace mis
{
	// Every read and write of Skyrim lives here, one function per game_hooks row.
	namespace Game
	{
		void LogHookTable();
		void InstallDataLoadedWatch();  // asi_boot -> data_loaded

		struct MusicNow
		{
			const void*  type{ nullptr };  // identity of Skyrim's current music type (nullptr: none)
			sheets::Pool pool{ sheets::Pool::silence };
			bool         known{ false };   // in the skyrim_music_types sheet
		};
		MusicNow CurrentMusic();                         // music_current
		void     SetSkyrimMusicMuted(bool a_muted);      // music_mute
		float    MasterVolume();                         // volume_sliders
		float    MusicVolume();                          // volume_sliders
		float    Hour();                                 // game_hour
		bool     GameHasFocus();                         // focus
		void     QueueNotice(std::string a_text);        // notify (shown on the game thread)
		std::string MusicTypeName(const void* a_type);   // for the log
	}
}
