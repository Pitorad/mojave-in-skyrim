#pragma once

struct ma_engine;

namespace mis
{
	// A playback lane (situation music, or the radio). It plays one clip at a time; starting another
	// crossfades, and older clips keep fading out until silent. Only the Director thread calls it.
	class Deck
	{
	public:
		explicit Deck(const char* a_name);
		~Deck();

		// Starts the clip (bytes of an .mp3 or .ogg), fading it in and anything playing out.
		bool Play(ma_engine* a_engine, std::vector<std::uint8_t> a_bytes, const std::string& a_label, float a_fadeSeconds);
		void FadeOut(float a_seconds);
		void StopNow();

		// Advances fades by a_dt seconds; a_volume is this deck's loudness (sliders x INI x focus).
		void Update(float a_dt, float a_volume);

		bool Playing() const;     // the current clip is still producing sound
		bool Finished() const;    // a clip was started and has reached its end
		const std::string& Current() const { return current; }

	private:
		struct Voice;
		const char*                         name;
		std::list<std::unique_ptr<Voice>>   voices;  // back() is the current clip
		std::string                         current;
	};

	class Audio
	{
	public:
		bool Init();
		void Shutdown();
		ma_engine* Engine() { return engine; }

	private:
		ma_engine* engine{ nullptr };
	};
}
