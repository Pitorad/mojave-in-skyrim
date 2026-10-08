#pragma once

#include "Bsa.h"
#include "generated/Sheets.h"

namespace mis
{
	// One playable piece of New Vegas audio: a loose file or an entry in one of its archives.
	struct Clip
	{
		fs::path               file;              // loose .mp3, or
		const Bsa*             bsa{ nullptr };    // an archive entry
		const Bsa::Entry*      entry{ nullptr };
		std::string            label;             // file name, or the song title

		std::vector<std::uint8_t> Load() const;
	};

	// A Mr. New Vegas line: one or more parts that play back to back.
	using DjLine = std::vector<Clip>;

	// Everything the sheets name, found in the player's own New Vegas install.
	class Library
	{
	public:
		bool Build(const fs::path& a_fnv);

		// The pool's tracks for this hour (day/night pools split by the settings' hours).
		const std::vector<Clip>& PoolTracks(sheets::Pool a_pool, bool a_night) const;
		const std::vector<Clip>& Songs() const { return songs; }
		const std::vector<DjLine>& Dj(sheets::DjRole a_role) const;

	private:
		struct PoolFiles
		{
			std::vector<Clip> day, night;
		};
		Bsa                                                      sound, voices;
		std::array<PoolFiles, std::to_underlying(sheets::Pool::kCount)> pools;
		std::vector<Clip>                                        songs;
		std::array<std::vector<DjLine>, 5>                       dj;
	};
}
