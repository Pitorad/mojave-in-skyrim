#include "Director.h"

#include "Audio.h"
#include "Game.h"
#include "Library.h"
#include "Settings.h"

namespace mis::Director
{
	using sheets::DjRole;
	using sheets::Pool;
	using sheets::Repeat;

	namespace
	{
		std::atomic<bool> g_radioWanted{ false };
		std::atomic<bool> g_running{ false };

		const sheets::MusicPool& PoolRow(Pool a_pool) { return sheets::kPools[std::to_underlying(a_pool)]; }

		class Runner
		{
		public:
			void Run()
			{
				const auto fnv = Settings::FalloutNVFolder();
				if (!fnv) {
					logger::error("Fallout: New Vegas wasn't found; Skyrim's own music plays");
					Game::QueueNotice("Mojave in Skyrim: Fallout: New Vegas wasn't found, so Skyrim's music plays as usual.");
					return;
				}
				if (!library.Build(*fnv) || !audio.Init()) {
					Game::QueueNotice("Mojave in Skyrim couldn't read New Vegas's music; Skyrim's music plays as usual.");
					return;
				}
				logger::info("director: running (radio key {:#x})", Settings::Get().radio_iToggleKey);
				auto last = std::chrono::steady_clock::now();
				for (;;) {
					std::this_thread::sleep_for(50ms);
					const auto now = std::chrono::steady_clock::now();
					const float dt = std::min(std::chrono::duration<float>(now - last).count(), 0.25f);
					last = now;
					Tick(dt);
				}
			}

		private:
			void Tick(float a_dt)
			{
				const auto& s = Settings::Get();
				snap = Game::Sample();  // taken on the game thread; no game objects are touched here
				if (!snap.valid) {
					return;  // no frame sampled yet
				}
				const auto& music = snap;
				if (music.type != lastType) {
					logger::info("skyrim music: {} -> pool {}{}", music.typeName,
						music.type ? PoolRow(music.pool).name : "(keep)", music.known || !music.type ? "" : " (not in the sheet)");
					lastType = music.type;
				}
				// No music type: Skyrim is between pieces. Keep the last background pool going.
				const Pool want = music.type ? music.pool : lastLoopPool;
				if (music.type && PoolRow(want).repeat == Repeat::kLoop && want != Pool::skyrim) {
					lastLoopPool = want;
				}

				const bool skyrimMoment = want == Pool::skyrim;
				Game::WantSkyrimMusicMuted(!skyrimMoment);

				// Radio on/off.
				const bool radioWanted = g_radioWanted;
				if (radioWanted != radioOn) {
					radioOn = radioWanted;
					if (radioOn) {
						musicDeck.FadeOut(1.0f);
						radioQueue.clear();
						if (s.radio_bGreetOnSwitchOn) {
							QueueDj(DjRole::kSwitchOn);
						}
						Game::QueueNotice("Radio New Vegas: on");
						logger::info("radio on");
					} else {
						radioDeck.FadeOut(0.7f);
						radioQueue.clear();
						Game::QueueNotice("Radio New Vegas: off");
						logger::info("radio off");
						playingPool.reset();  // resume situation music below
					}
				}

				// Situation music.
				const bool wantMusic = !radioOn && !skyrimMoment && want != Pool::silence;
				if (playingPool != want) {
					playingPool = want;
					poolDone = false;
					if (wantMusic) {
						StartPoolTrack(want, s.music_fCrossfadeSeconds);
					} else {
						musicDeck.FadeOut(s.music_fCrossfadeSeconds);
					}
				} else if (wantMusic && musicDeck.Finished() && !poolDone) {
					if (PoolRow(want).repeat == Repeat::kLoop) {
						StartPoolTrack(want, 0.5f);
					} else {
						poolDone = true;
					}
				}

				// Radio.
				if (radioOn && (radioDeck.Current().empty() || radioDeck.Finished())) {
					if (radioQueue.empty()) {
						FillRadio();
					}
					if (!radioQueue.empty()) {
						auto item = std::move(radioQueue.front());
						radioQueue.erase(radioQueue.begin());
						if (radioDeck.Play(audio.Engine(), item.clip.Load(), item.clip.label, 0.05f) && item.song) {
							Game::QueueNotice("Radio New Vegas: " + item.clip.label);
							logger::info("radio: {}", item.clip.label);
						}
					}
				}

				// Loudness: Skyrim's sliders x INI x focus.
				const float focusTarget = (!s.music_bQuietWhenUnfocused || Game::GameHasFocus()) ? 1.0f : 0.0f;
				focus += std::clamp(focusTarget - focus, -a_dt * 2.0f, a_dt * 2.0f);
				const float master = snap.master;
				musicDeck.Update(a_dt, master * snap.music * s.music_fVolume * focus);
				radioDeck.Update(a_dt, master * s.radio_fVolume * focus);

				if (++ticksSinceVolumeLog >= 1200) {  // once a minute
					ticksSinceVolumeLog = 0;
					logger::info("volume: master {:.2f}, music {:.2f}, hour {:.1f}, focus {:.0f}, music '{}', radio '{}'",
						master, snap.music, snap.hour, focus, musicDeck.Current(), radioDeck.Current());
				}
			}

			bool IsNight() const
			{
				const auto& s = Settings::Get();
				const int h = static_cast<int>(snap.hour);
				const int day = s.music_iDayStartHour, night = s.music_iNightStartHour;
				return day <= night ? (h < day || h >= night) : (h >= night && h < day);
			}

			void StartPoolTrack(Pool a_pool, float a_fade)
			{
				const auto& tracks = library.PoolTracks(a_pool, IsNight());
				if (tracks.empty()) {
					musicDeck.FadeOut(a_fade);
					return;
				}
				std::size_t pick = std::uniform_int_distribution<std::size_t>(0, tracks.size() - 1)(rng);
				if (tracks.size() > 1 && tracks[pick].label == lastTrack) {
					pick = (pick + 1) % tracks.size();
				}
				lastTrack = tracks[pick].label;
				if (musicDeck.Play(audio.Engine(), tracks[pick].Load(), tracks[pick].label, a_fade)) {
					logger::info("music: {} ({})", tracks[pick].label, PoolRow(a_pool).label);
				}
			}

			struct RadioItem
			{
				Clip clip;
				bool song{ false };
			};

			void QueueDj(DjRole a_role)
			{
				const auto& lines = library.Dj(a_role);
				if (lines.empty()) {
					return;
				}
				const auto& line = lines[std::uniform_int_distribution<std::size_t>(0, lines.size() - 1)(rng)];
				for (const auto& part : line) {
					radioQueue.push_back({ part, false });
				}
			}

			void QueueStories()
			{
				// The story categories share a role; pick across all of them.
				const auto& stories = library.Dj(DjRole::kNewsStory);
				if (!stories.empty()) {
					for (const auto& part : stories[std::uniform_int_distribution<std::size_t>(0, stories.size() - 1)(rng)]) {
						radioQueue.push_back({ part, false });
					}
				}
			}

			void FillRadio()
			{
				const auto& s = Settings::Get();
				const auto& songs = library.Songs();
				if (songs.empty()) {
					return;
				}
				if (s.radio_iNewsEverySongs > 0 && songsSinceNews >= s.radio_iNewsEverySongs) {
					songsSinceNews = 0;
					QueueDj(DjRole::kNewsOpen);
					QueueStories();
					QueueDj(DjRole::kNewsClose);
				}
				if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < s.radio_fSongIntroChance) {
					QueueDj(DjRole::kBeforeSong);
				}
				// Shuffle bag: every song once before any repeats.
				if (songBag.empty()) {
					songBag.resize(songs.size());
					std::iota(songBag.begin(), songBag.end(), std::size_t{ 0 });
					std::ranges::shuffle(songBag, rng);
				}
				radioQueue.push_back({ songs[songBag.back()], true });
				songBag.pop_back();
				++songsSinceNews;
			}

			Library                  library;
			Audio                    audio;
			Deck                     musicDeck{ "music" };
			Deck                     radioDeck{ "radio" };
			std::mt19937             rng{ std::random_device{}() };
			std::uintptr_t           lastType{ 1 };
			Game::Snapshot           snap;
			Pool                     lastLoopPool{ Pool::silence };
			std::optional<Pool>      playingPool;
			bool                     poolDone{ false };
			bool                     radioOn{ false };
			std::vector<RadioItem>   radioQueue;
			std::vector<std::size_t> songBag;
			int                      songsSinceNews{ 0 };
			std::string              lastTrack;
			float                    focus{ 1.0f };
			int                      ticksSinceVolumeLog{ 0 };
		};
	}

	void Start()
	{
		if (g_running.exchange(true)) {
			return;
		}
		std::thread([] {
			static Runner runner;
			runner.Run();
		}).detach();
	}

	void ToggleRadio()
	{
		g_radioWanted = !g_radioWanted;
	}
}
