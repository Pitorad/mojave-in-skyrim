#include "Audio.h"

#include "miniaudio.h"

namespace mis
{
	struct Deck::Voice
	{
		std::vector<std::uint8_t> bytes;
		ma_decoder                decoder{};
		ma_sound                  sound{};
		bool                      decoderReady{ false };
		bool                      soundReady{ false };
		float                     gain{ 0.0f };    // fade position, 0..1
		float                     target{ 1.0f };
		float                     rate{ 1.0f };    // gain per second

		~Voice()
		{
			if (soundReady) {
				ma_sound_uninit(&sound);
			}
			if (decoderReady) {
				ma_decoder_uninit(&decoder);
			}
		}
	};

	Deck::Deck(const char* a_name) : name(a_name) {}
	Deck::~Deck() { StopNow(); }

	bool Deck::Play(ma_engine* a_engine, std::vector<std::uint8_t> a_bytes, const std::string& a_label, float a_fadeSeconds)
	{
		if (!a_engine || a_bytes.empty()) {
			return false;
		}
		auto v = std::make_unique<Voice>();
		v->bytes = std::move(a_bytes);
		auto cfg = ma_decoder_config_init(ma_format_f32, 0, ma_engine_get_sample_rate(a_engine));
		if (ma_decoder_init_memory(v->bytes.data(), v->bytes.size(), &cfg, &v->decoder) != MA_SUCCESS) {
			logger::warn("{}: can't decode {}", name, a_label);
			return false;
		}
		v->decoderReady = true;
		const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
		if (ma_sound_init_from_data_source(a_engine, &v->decoder, flags, nullptr, &v->sound) != MA_SUCCESS) {
			logger::warn("{}: can't play {}", name, a_label);
			return false;
		}
		v->soundReady = true;
		const float fade = std::max(a_fadeSeconds, 0.01f);
		v->rate = 1.0f / fade;
		v->gain = a_fadeSeconds <= 0.0f ? 1.0f : 0.0f;
		ma_sound_set_volume(&v->sound, 0.0f);
		ma_sound_start(&v->sound);
		for (auto& old : voices) {
			old->target = 0.0f;
			old->rate = 1.0f / fade;
		}
		voices.push_back(std::move(v));
		current = a_label;
		logger::debug("{}: play {}", name, a_label);
		return true;
	}

	void Deck::FadeOut(float a_seconds)
	{
		for (auto& v : voices) {
			v->target = 0.0f;
			v->rate = 1.0f / std::max(a_seconds, 0.01f);
		}
		current.clear();
	}

	void Deck::StopNow()
	{
		voices.clear();
		current.clear();
	}

	void Deck::Update(float a_dt, float a_volume)
	{
		for (auto it = voices.begin(); it != voices.end();) {
			auto& v = **it;
			if (v.gain < v.target) {
				v.gain = std::min(v.target, v.gain + v.rate * a_dt);
			} else if (v.gain > v.target) {
				v.gain = std::max(v.target, v.gain - v.rate * a_dt);
			}
			const bool faded = v.target == 0.0f && v.gain == 0.0f;
			const bool ended = ma_sound_at_end(&v.sound);
			// The current clip stays in the list when it ends, so Finished() can report it.
			if (faded || (ended && std::next(it) != voices.end())) {
				it = voices.erase(it);
				continue;
			}
			// Equal-power curve keeps the crossfade from dipping in the middle.
			ma_sound_set_volume(&v.sound, std::sin(v.gain * 1.5707964f) * a_volume);
			++it;
		}
	}

	bool Deck::Playing() const
	{
		return !voices.empty() && !current.empty() && voices.back()->target > 0.0f && !ma_sound_at_end(&voices.back()->sound);
	}

	bool Deck::Finished() const
	{
		return !voices.empty() && !current.empty() && ma_sound_at_end(&voices.back()->sound);
	}

	bool Audio::Init()
	{
		engine = new ma_engine{};
		if (ma_engine_init(nullptr, engine) != MA_SUCCESS) {
			logger::error("audio: no output device");
			delete engine;
			engine = nullptr;
			return false;
		}
		logger::info("audio: {} Hz, {} channels", ma_engine_get_sample_rate(engine), ma_engine_get_channels(engine));
		return true;
	}

	void Audio::Shutdown()
	{
		if (engine) {
			ma_engine_uninit(engine);
			delete engine;
			engine = nullptr;
		}
	}
}
