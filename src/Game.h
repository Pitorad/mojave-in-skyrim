#pragma once

#include "generated/Sheets.h"

namespace mis
{
	// Every read and write of Skyrim lives here, one function per game_hooks row. Game state is read
	// only on the game thread (every frame, from the input sink) into a snapshot; the Director's thread
	// reads the snapshot and never touches game objects, which Skyrim frees and replaces during loads.
	namespace Game
	{
		void LogHookTable();
		void InstallDataLoadedWatch();  // asi_boot -> data_loaded

		struct Snapshot
		{
			bool          valid{ false };       // at least one frame sampled
			std::uintptr_t type{ 0 };           // identity of Skyrim's current music type, only compared (0: none)
			std::string   typeName;             // for the log
			sheets::Pool  pool{ sheets::Pool::silence };
			bool          known{ false };       // in the skyrim_music_types sheet (or Skyrim's NoMusic)
			float         master{ 1.0f };       // volume_sliders
			float         music{ 1.0f };
			float         hour{ 12.0f };        // game_hour
		};
		Snapshot Sample();                       // music_current, volume_sliders, game_hour (copy of the last frame)
		void     WantSkyrimMusicMuted(bool a_muted);  // music_mute (applied on the game thread)
		bool     GameHasFocus();                 // focus
		void     QueueNotice(std::string a_text);  // notify (shown on the game thread)
	}
}
