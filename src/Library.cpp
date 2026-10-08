#include "Library.h"

namespace mis
{
	namespace
	{
		std::string Lower(std::string_view a_s)
		{
			std::string s(a_s);
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		bool IsMp3(const fs::path& a_p) { return Lower(a_p.extension().string()) == ".mp3"; }
	}

	std::vector<std::uint8_t> Clip::Load() const
	{
		if (bsa && entry) {
			return bsa->Read(*entry);
		}
		std::ifstream in(file, std::ios::binary | std::ios::ate);
		if (!in) {
			logger::warn("can't open {}", file.string());
			return {};
		}
		std::vector<std::uint8_t> data(static_cast<std::size_t>(in.tellg()));
		in.seekg(0);
		in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
		return data;
	}

	bool Library::Build(const fs::path& a_fnv)
	{
		using namespace sheets;
		std::error_code ec;

		// music_pools: loose .mp3 under Data/Music.
		const auto music = a_fnv / kSource_music.path;
		std::size_t poolFiles = 0;
		for (const auto& pool : kPools) {
			auto& out = pools[std::to_underlying(pool.id)];
			for (const auto& src : pool.sources) {
				const auto p = music / fs::u8path(src);
				std::vector<fs::path> files;
				if (fs::is_directory(p, ec)) {
					for (const auto& f : fs::directory_iterator(p, ec)) {
						if (f.is_regular_file() && IsMp3(f.path())) {
							files.push_back(f.path());
						}
					}
				} else if (fs::is_regular_file(p, ec)) {
					files.push_back(p);
				} else {
					logger::warn("pool {}: {} is missing", pool.name, p.string());
				}
				std::ranges::sort(files);
				for (auto& f : files) {
					const auto name = Lower(f.filename().string());
					const bool night = pool.dayNight && name.find("_night_") != std::string::npos;
					(night ? out.night : out.day).push_back({ f, nullptr, nullptr, f.filename().string() });
					++poolFiles;
				}
			}
			if (!pool.dayNight) {
				out.night = out.day;
			}
		}

		// radio_songs: inside Fallout - Sound.bsa.
		if (sound.Open(a_fnv / kSource_songs.path)) {
			const auto inFolder = sound.Folder(kSource_songs.inner);
			for (const auto& song : kSongs) {
				const auto want = Lower(song.file);
				const auto it = std::ranges::find_if(inFolder, [&](const Bsa::Entry* e) { return e->name == want; });
				if (it == inFolder.end()) {
					logger::warn("radio: {} ({}) isn't in this copy of New Vegas", song.title, song.file);
					continue;
				}
				songs.push_back({ {}, &sound, *it, std::string(song.title) });
			}
		}

		// radio_dj: <prefix>_<formid>_<n>.ogg inside Fallout - Voices1.bsa; parts of one form ID play in order.
		std::size_t lines = 0;
		if (voices.Open(a_fnv / kSource_voices.path)) {
			const auto inFolder = voices.Folder(kSource_voices.inner);
			for (const auto& cat : kDj) {
				const auto prefix = Lower(cat.prefix) + "_";
				std::map<std::string, std::map<int, const Bsa::Entry*>> byForm;
				for (const auto* e : inFolder) {
					if (!e->name.starts_with(prefix) || !e->name.ends_with(".ogg")) {
						continue;
					}
					// rest = <formid>_<n>.ogg
					const auto rest = e->name.substr(prefix.size(), e->name.size() - prefix.size() - 4);
					const auto us = rest.find('_');
					if (us != 8) {
						continue;  // a longer topic sharing this prefix
					}
					byForm[rest.substr(0, us)][std::atoi(rest.c_str() + us + 1)] = e;
				}
				auto& out = dj[std::to_underlying(cat.role)];
				for (auto& [form, parts] : byForm) {
					DjLine line;
					for (auto& [n, e] : parts) {
						line.push_back({ {}, &voices, e, e->name });
					}
					out.push_back(std::move(line));
					++lines;
				}
				if (byForm.empty()) {
					logger::warn("radio: no Mr. New Vegas lines for {}", cat.id);
				}
			}
		}

		logger::info("library: {} music tracks, {}/{} songs, {} Mr. New Vegas lines", poolFiles, songs.size(), kSongs.size(), lines);
		return poolFiles > 0 || !songs.empty();
	}

	const std::vector<Clip>& Library::PoolTracks(sheets::Pool a_pool, bool a_night) const
	{
		const auto& p = pools[std::to_underlying(a_pool)];
		return a_night ? p.night : p.day;
	}

	const std::vector<DjLine>& Library::Dj(sheets::DjRole a_role) const
	{
		return dj[std::to_underlying(a_role)];
	}
}
