// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "fvd_audio.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "../log/log.h"
#include "../util/file.h"
#include "../util/path.h"
#include "error.h"
#include "mixer.h"
#include "output.h"

namespace openage::audio {

namespace {

struct CueRule {
	const char *name;
	/** Level before the effects/master volume. */
	float gain;
	/** Throttle burst and sustained rate. */
	float burst;
	float per_second;
};

constexpr std::array<CueRule, CUE_COUNT> RULES{{
	{"hit", 0.6f, 4.0f, 10.0f},
	{"arrow_shot", 0.45f, 4.0f, 8.0f},
	{"tower_arrow_shot", 0.55f, 5.0f, 10.0f},
	{"death_horse", 0.9f, 3.0f, 4.0f},
	{"death_builder", 0.9f, 3.0f, 4.0f},
	{"death_tower", 0.8f, 3.0f, 4.0f},
	{"foundation_placed", 0.5f, 3.0f, 8.0f},
	{"tower_built", 0.7f, 3.0f, 4.0f},
	{"order", 0.6f, 1.0f, 5.0f},
	{"hunter_unleashed", 1.0f, 1.0f, 0.2f},
	{"flare", 0.8f, 3.0f, 2.0f},
}};

constexpr float MUSIC_FADE_SECONDS = 2.0f;

/** How far pan follows the screen position; full hard-panning is tiring. */
constexpr float PAN_WIDTH = 0.7f;

/** Split "name_3.opus" into stem "name" when the part after '_' is a number. */
std::optional<std::string> sound_stem(const std::string &file_name) {
	if (not is_supported_sound_file(file_name)) {
		return std::nullopt;
	}
	std::string stem = file_name.substr(0, file_name.rfind('.'));
	const auto underscore = stem.rfind('_');
	if (underscore != std::string::npos and underscore + 1 < stem.size()
	    and std::all_of(stem.begin() + underscore + 1, stem.end(), [](unsigned char c) {
		    return std::isdigit(c);
	    })) {
		stem.resize(underscore);
	}
	return stem;
}

const char *music_stem(MusicState state) {
	switch (state) {
	case MusicState::LOBBY:
		return "lobby";
	case MusicState::MATCH:
		return "match";
	default:
		return nullptr;
	}
}

} // namespace

const char *cue_name(Cue cue) {
	return RULES.at(static_cast<size_t>(cue)).name;
}

std::optional<Cue> cue_for_file(const std::string &file_name) {
	const auto stem = sound_stem(file_name);
	if (not stem) {
		return std::nullopt;
	}
	for (size_t i = 0; i < CUE_COUNT; ++i) {
		if (*stem == RULES[i].name) {
			return static_cast<Cue>(i);
		}
	}
	return std::nullopt;
}

bool is_music_file(const std::string &file_name, MusicState state) {
	const auto stem = sound_stem(file_name);
	const char *wanted = music_stem(state);
	return stem and wanted and *stem == wanted;
}

std::optional<Placement> place_on_screen(const Eigen::Vector3f &world,
                                         const Eigen::Matrix4f &view_projection) {
	const Eigen::Vector4f clip = view_projection * Eigen::Vector4f{world.x(), world.y(), world.z(), 1.0f};
	if (not std::isfinite(clip.w()) or clip.w() <= 0.0f
	    or not std::isfinite(clip.x()) or not std::isfinite(clip.y())) {
		return std::nullopt;
	}
	const float x = clip.x() / clip.w();
	const float y = clip.y() / clip.w();
	const float outside = std::max(std::abs(x), std::abs(y)) - 1.0f;
	const float gain = outside <= 0.0f ? 1.0f : 1.0f - outside / EDGE_FALLOFF;
	if (gain <= 0.0f) {
		return std::nullopt;
	}
	return Placement{gain, std::clamp(x, -1.0f, 1.0f) * PAN_WIDTH};
}

Throttle::Throttle(float capacity, float per_second) :
	capacity{capacity},
	per_second{per_second},
	tokens{capacity} {}

bool Throttle::allow(clock::time_point now) {
	if (this->last) {
		const float elapsed = std::chrono::duration<float>(now - *this->last).count();
		this->tokens = std::min(this->capacity, this->tokens + std::max(0.0f, elapsed) * this->per_second);
	}
	this->last = now;
	if (this->tokens < 1.0f) {
		return false;
	}
	this->tokens -= 1.0f;
	return true;
}

void SoundBank::add(Cue cue, std::shared_ptr<const PcmClip> clip) {
	if (clip and clip->frames() > 0) {
		this->clips.at(static_cast<size_t>(cue)).push_back(std::move(clip));
	}
}

std::shared_ptr<const PcmClip> SoundBank::pick(Cue cue, std::minstd_rand &random) const {
	const auto &options = this->clips.at(static_cast<size_t>(cue));
	if (options.empty()) {
		return nullptr;
	}
	std::uniform_int_distribution<size_t> choose{0, options.size() - 1};
	return options[choose(random)];
}

size_t SoundBank::variants(Cue cue) const {
	return this->clips.at(static_cast<size_t>(cue)).size();
}

size_t SoundBank::fill_missing(const SoundBank &other) {
	size_t filled = 0;
	for (size_t i = 0; i < CUE_COUNT; ++i) {
		if (this->clips[i].empty() and not other.clips[i].empty()) {
			this->clips[i] = other.clips[i];
			++filled;
		}
	}
	return filled;
}

size_t SoundBank::total() const {
	size_t count = 0;
	for (const auto &options : this->clips) {
		count += options.size();
	}
	return count;
}

namespace {

// The old Python developer launcher synthesised these cues into a cache folder.
// The native launcher does not run Python, so provide equivalent temporary
// samples directly in memory. Real recordings and existing cached placeholders
// remain higher priority; no files are created or modified at runtime.
using mono_t = std::vector<float>;
constexpr float TAU = 6.2831853071795864769f;

float envelope(size_t i, size_t count, float attack, float decay) {
	const size_t rise = std::max<size_t>(1, static_cast<size_t>(attack * OUTPUT_RATE));
	if (i < rise) {
		return static_cast<float>(i) / static_cast<float>(rise);
	}
	if (decay > 0.0f) {
		return std::exp(-static_cast<float>(i - rise) / (decay * OUTPUT_RATE));
	}
	return 1.0f - static_cast<float>(i - rise)
	             / static_cast<float>(std::max<size_t>(1, count - rise));
}

mono_t tone(float start, float end, float seconds,
            std::initializer_list<float> harmonics = {1.0f}, float decay = 0.0f) {
	const size_t count = static_cast<size_t>(seconds * OUTPUT_RATE);
	mono_t out(count);
	double phase = 0.0;
	for (size_t i = 0; i < count; ++i) {
		const float frequency = start + (end - start) * static_cast<float>(i) / count;
		phase += static_cast<double>(TAU) * frequency / OUTPUT_RATE;
		float value = 0.0f;
		size_t harmonic = 1;
		for (const float amplitude : harmonics) {
			value += amplitude * static_cast<float>(std::sin(phase * harmonic++));
		}
		out[i] = value * envelope(i, count, 0.005f, decay);
	}
	return out;
}

mono_t noise(float seconds, float lowpass, float decay, unsigned seed) {
	const size_t count = static_cast<size_t>(seconds * OUTPUT_RATE);
	mono_t out(count);
	std::minstd_rand random{seed};
	std::uniform_real_distribution<float> uniform{-1.0f, 1.0f};
	float level = 0.0f;
	for (size_t i = 0; i < count; ++i) {
		level += lowpass * (uniform(random) - level);
		out[i] = level * envelope(i, count, 0.002f, decay);
	}
	return out;
}

mono_t mix(std::initializer_list<mono_t> parts);
mono_t silence(float seconds);

/** One exponentially decaying partial of a struck or resonant body. */
struct Partial {
	float frequency;
	float gain;
	float decay;
	float phase;
};

float attack_release(float time, float seconds, float attack, float release) {
	const float fade_in = attack > 0.0f ? std::min(1.0f, time / attack) : 1.0f;
	const float remaining = seconds - time;
	const float fade_out = release > 0.0f ? std::min(1.0f, std::max(0.0f, remaining) / release) : 1.0f;
	return std::min(fade_in, fade_out);
}

mono_t partials(float seconds, std::initializer_list<Partial> modes,
               float attack = 0.002f, float release = 0.02f) {
	const size_t count = static_cast<size_t>(seconds * OUTPUT_RATE);
	mono_t out(count);
	for (size_t i = 0; i < count; ++i) {
		const float time = static_cast<float>(i) / OUTPUT_RATE;
		float sample = 0.0f;
		for (const auto &mode : modes) {
			sample += mode.gain * std::exp(-time / mode.decay)
			          * static_cast<float>(std::sin(TAU * mode.frequency * time + mode.phase));
		}
		out[i] = sample * attack_release(time, seconds, attack, release);
	}
	return out;
}

/** Mix a sound into another one after a delay, growing the target as needed. */
void layer_at(mono_t &target, const mono_t &layer, float delay) {
	const size_t first = static_cast<size_t>(std::max(0.0f, delay) * OUTPUT_RATE);
	if (target.size() < first + layer.size()) {
		target.resize(first + layer.size());
	}
	for (size_t i = 0; i < layer.size(); ++i) {
		target[first + i] += layer[i];
	}
}

/** A close, woody impact: a sharp broad-band onset and short body modes. */
mono_t wood_impact(float pitch, float length, unsigned seed) {
	return mix({
		noise(0.09f * length, 0.68f, 0.009f * length, seed),
		partials(0.24f * length,
		         {{145.0f * pitch, 0.74f, 0.050f * length, 0.0f},
		          {365.0f * pitch, 0.33f, 0.032f * length, 0.4f},
		          {860.0f * pitch, 0.13f, 0.018f * length, 1.1f}},
		         0.001f, 0.035f * length),
	});
}

/** A hard strike with inharmonic, bright metal-like modes. */
mono_t metal_impact(float pitch, float length, unsigned seed) {
	return mix({
		noise(0.055f * length, 0.86f, 0.006f * length, seed),
		partials(0.36f * length,
		         {{280.0f * pitch, 0.34f, 0.110f * length, 0.0f},
		          {735.0f * pitch, 0.30f, 0.090f * length, 0.9f},
		          {1470.0f * pitch, 0.21f, 0.065f * length, 1.9f},
		          {2860.0f * pitch, 0.13f, 0.034f * length, 2.7f}},
		         0.001f, 0.045f * length),
	});
}

/** A noise-excited air resonance, with continuous state during a frequency sweep. */
struct AirResonator {
	float first{0.0f};
	float second{0.0f};

	float process(float input, float frequency, float quality) {
		const float g = std::tan(0.5f * TAU * frequency / OUTPUT_RATE);
		const float damping = 1.0f / quality;
		const float a = 1.0f / (1.0f + g * (g + damping));
		const float v = input - this->second;
		const float band = a * this->first + g * a * v;
		const float low = this->second + g * a * this->first + g * g * a * v;
		this->first = 2.0f * band - this->first;
		this->second = 2.0f * low - this->second;
		return damping * band;
	}
};

/** A horse-like call: a breathy formant voice with a fast rise and gliding fall. */
mono_t horse_whinny(float pitch, float duration, float breathiness, unsigned seed) {
	constexpr float BASE_DURATION = 1.18f;
	const size_t count = static_cast<size_t>(BASE_DURATION * duration * OUTPUT_RATE);
	mono_t out(count);
	std::minstd_rand random{seed};
	std::uniform_real_distribution<float> uniform{-1.0f, 1.0f};
	AirResonator chest{};
	AirResonator muzzle{};
	AirResonator edge{};
	double phase = 0.0;
	for (size_t i = 0; i < count; ++i) {
		const float time = BASE_DURATION * static_cast<float>(i) / static_cast<float>(count - 1);
		const float rise = std::min(1.0f, time / 0.34f);
		const float fall = std::max(0.0f, (time - 0.34f) / (BASE_DURATION - 0.34f));
		const float frequency = pitch * ((155.0f + 500.0f * rise) * (1.0f - fall)
		                                 + (355.0f - 90.0f * fall) * fall);
		const float vibrato = 1.0f + 0.042f * static_cast<float>(std::sin(TAU * 6.7f * time));
		phase += static_cast<double>(TAU) * frequency * vibrato / OUTPUT_RATE;
		const float glottal = 0.76f * static_cast<float>(std::sin(phase))
		                      + 0.32f * static_cast<float>(std::sin(phase * 2.0))
		                      + 0.16f * static_cast<float>(std::sin(phase * 3.0));
		const float voice = 1.35f * chest.process(glottal, 820.0f, 4.5f)
		                    + 1.05f * muzzle.process(glottal, 1740.0f, 6.0f)
		                    + 0.42f * edge.process(glottal, 3140.0f, 7.0f);
		const float breath = uniform(random);
		const float envelope = attack_release(time, BASE_DURATION, 0.035f, 0.22f)
		                       * (0.88f + 0.12f * static_cast<float>(std::sin(TAU * 3.1f * time)));
		out[i] = envelope * (voice + breathiness * 0.10f * breath);
	}
	return out;
}

/** An intentionally non-verbal, short distressed cry followed by a body thump. */
mono_t builder_fall(float pitch, float duration, float breathiness, unsigned seed) {
	constexpr float BASE_DURATION = 0.54f;
	const size_t count = static_cast<size_t>(BASE_DURATION * duration * OUTPUT_RATE);
	mono_t out(count);
	std::minstd_rand random{seed};
	std::uniform_real_distribution<float> uniform{-1.0f, 1.0f};
	AirResonator chest{};
	AirResonator mouth{};
	AirResonator sibilance{};
	double phase = 0.0;
	for (size_t i = 0; i < count; ++i) {
		const float time = BASE_DURATION * static_cast<float>(i) / static_cast<float>(count - 1);
		const float frequency = pitch * (205.0f - 120.0f * time
		                                 + 12.0f * static_cast<float>(std::sin(TAU * 8.0f * time)));
		phase += static_cast<double>(TAU) * frequency / OUTPUT_RATE;
		const float glottal = 0.82f * static_cast<float>(std::sin(phase))
		                      + 0.24f * static_cast<float>(std::sin(phase * 2.0));
		const float voice = 1.25f * chest.process(glottal, 620.0f, 4.2f)
		                    + 1.04f * mouth.process(glottal, 1210.0f, 5.5f)
		                    + 0.28f * sibilance.process(uniform(random), 2540.0f, 2.4f);
		const float irregular = 0.78f + 0.18f * static_cast<float>(std::sin(TAU * 11.0f * time));
		const float envelope = attack_release(time, BASE_DURATION, 0.018f, 0.16f) * irregular;
		out[i] = envelope * (voice + breathiness * 0.075f * uniform(random));
	}
	layer_at(out, wood_impact(0.82f * pitch, 0.65f, seed + 1), 0.31f * duration);
	return out;
}

/** A brassy horn note: breath, a rounded attack and a harmonic lip buzz. */
mono_t brass_call(float start, float end, float seconds, float breathiness, unsigned seed) {
	const size_t count = static_cast<size_t>(seconds * OUTPUT_RATE);
	mono_t out(count);
	std::minstd_rand random{seed};
	std::uniform_real_distribution<float> uniform{-1.0f, 1.0f};
	double phase = 0.0;
	for (size_t i = 0; i < count; ++i) {
		const float time = static_cast<float>(i) / OUTPUT_RATE;
		const float progress = time / seconds;
		const float frequency = start + (end - start) * progress
		                        + 1.5f * static_cast<float>(std::sin(TAU * 5.1f * time));
		phase += static_cast<double>(TAU) * frequency / OUTPUT_RATE;
		const float buzz = 0.78f * static_cast<float>(std::sin(phase))
		                   + 0.44f * static_cast<float>(std::sin(phase * 2.0))
		                   + 0.25f * static_cast<float>(std::sin(phase * 3.0))
		                   + 0.11f * static_cast<float>(std::sin(phase * 4.0));
		const float envelope = attack_release(time, seconds, 0.065f, 0.28f);
		out[i] = envelope * (buzz + breathiness * 0.055f * uniform(random));
	}
	return out;
}

/** A compact, inharmonic struck chime rather than a pure sine ping. */
mono_t chime(float pitch, float seconds, unsigned seed) {
	return mix({
		noise(0.018f, 0.91f, 0.0035f, seed),
		partials(seconds,
		         {{pitch, 0.84f, seconds * 0.50f, 0.0f},
		          {pitch * 1.49f, 0.42f, seconds * 0.35f, 0.7f},
		          {pitch * 2.67f, 0.31f, seconds * 0.24f, 1.8f},
		          {pitch * 4.18f, 0.17f, seconds * 0.15f, 2.4f}},
		         0.001f, 0.05f),
	});
}

mono_t tower_collapse(float scale, unsigned seed) {
	mono_t out = mix({
		noise(1.85f * scale, 0.045f, 0.62f * scale, seed),
		partials(1.45f * scale,
		         {{62.0f, 0.68f, 0.54f * scale, 0.0f},
		          {103.0f, 0.33f, 0.35f * scale, 1.0f},
		          {188.0f, 0.16f, 0.21f * scale, 2.0f}},
		         0.004f, 0.24f * scale),
	});
	constexpr float OFFSETS[]{0.01f, 0.13f, 0.27f, 0.49f, 0.70f, 0.93f, 1.21f};
	for (size_t i = 0; i < sizeof(OFFSETS) / sizeof(OFFSETS[0]); ++i) {
		const float size = 1.08f - 0.075f * static_cast<float>(i);
		layer_at(out, wood_impact(0.72f + 0.10f * static_cast<float>((seed + i) % 4), size, seed + 8 + i),
		         OFFSETS[i] * scale);
	}
	return out;
}

mono_t foundation_placed(float pace, unsigned seed) {
	mono_t out = silence(0.025f * pace);
	constexpr float OFFSETS[]{0.025f, 0.085f, 0.168f, 0.243f};
	for (size_t i = 0; i < sizeof(OFFSETS) / sizeof(OFFSETS[0]); ++i) {
		layer_at(out,
		         wood_impact(0.90f + 0.08f * static_cast<float>((seed + i) % 3),
		                     0.72f - 0.05f * static_cast<float>(i), seed + i),
		         OFFSETS[i] * pace);
	}
	return out;
}

mono_t tower_built(float pitch, unsigned seed) {
	mono_t out = wood_impact(0.84f * pitch, 0.90f, seed);
	layer_at(out, brass_call(261.6f * pitch, 329.6f * pitch, 0.25f, 0.07f, seed + 1), 0.10f);
	layer_at(out, brass_call(392.0f * pitch, 392.0f * pitch, 0.58f, 0.04f, seed + 2), 0.29f);
	return out;
}

mono_t order_acknowledgement(float pitch, unsigned seed) {
	mono_t out = chime(770.0f * pitch, 0.13f, seed);
	layer_at(out, chime(1675.0f * pitch, 0.075f, seed + 1), 0.105f);
	return out;
}

/** A smooth crest with an exponential approach and decay on either side. */
float swish_envelope(float offset, float width) {
	return 1.0f / std::cosh(offset / width);
}

mono_t arrow_shot(float pitch, float time_scale, float brightness, unsigned seed) {
	constexpr float seconds = 0.250f;
	const size_t count = static_cast<size_t>(seconds * time_scale * OUTPUT_RATE);
	mono_t out(count);
	std::minstd_rand random{seed};
	std::uniform_real_distribution<float> uniform{-1.0f, 1.0f};
	AirResonator whistle{};
	AirResonator rush{};
	AirResonator fletching{};
	AirResonator wake{};
	const float whistle_gain = 1.0f / std::sqrt(brightness);
	double phase = 0.0;
	for (size_t i = 0; i < count; ++i) {
		const float time = seconds * static_cast<float>(i) / static_cast<float>(count - 1);
		const float offset = time - 0.110f;
		const float gust = uniform(random);
		const float pressure = wake.process(gust, 70.0f * pitch, 1.2f);

		// The whistle rises on approach and drops faster as the arrow passes.
		const float bend = swish_envelope(offset, offset < 0.0f ? 0.030f : 0.021f);
		const float frequency = pitch * (545.0f + 265.0f * bend) * (1.0f + 0.25f * pressure);
		phase += static_cast<double>(TAU) * frequency / OUTPUT_RATE;
		const float breath = whistle.process(gust, frequency, 9.0f);
		const float whistling = (0.074f * static_cast<float>(std::sin(phase)) + 0.92f * breath)
		                        * swish_envelope(offset - 0.004f, 0.033f);

		// The broadband rush briefly covers the whistle. The brightest air
		// crests a few milliseconds later and gives way to a quiet wake.
		const float air = rush.process(uniform(random), 1.95f * frequency, 0.85f);
		const float edge = fletching.process(uniform(random), pitch * (8200.0f + 3100.0f * bend), 0.70f);
		const float rush_gain = 0.87f * swish_envelope(offset + 0.003f, 0.017f);
		const float edge_gain = 0.36f * brightness * swish_envelope(offset - 0.005f, 0.017f);
		const float wake_gain = 1.10f * swish_envelope(offset - 0.045f, 0.029f);
		const float fade = std::min({time / 0.012f, (seconds - time) / 0.012f, 1.0f});
		out[i] = fade * (whistle_gain * whistling + rush_gain * air + edge_gain * edge + wake_gain * pressure);
	}
	return out;
}

mono_t mix(std::initializer_list<mono_t> parts) {
	size_t frames = 0;
	for (const auto &part : parts) {
		frames = std::max(frames, part.size());
	}
	mono_t out(frames, 0.0f);
	for (const auto &part : parts) {
		for (size_t i = 0; i < part.size(); ++i) {
			out[i] += part[i];
		}
	}
	return out;
}

mono_t concat(mono_t first, const mono_t &second) {
	first.insert(first.end(), second.begin(), second.end());
	return first;
}

mono_t silence(float seconds) {
	return mono_t(static_cast<size_t>(seconds * OUTPUT_RATE), 0.0f);
}

mono_t note(float frequency, float seconds) {
	return tone(frequency, frequency, seconds, {1.0f, 0.3f, 0.1f}, seconds / 3.0f);
}

mono_t sequence(std::initializer_list<float> frequencies, int repetitions, float step) {
	mono_t result{};
	for (int repeat = 0; repeat < repetitions; ++repeat) {
		for (const float frequency : frequencies) {
			const auto part = frequency > 0.0f ? note(frequency, step) : silence(step);
			result.insert(result.end(), part.begin(), part.end());
		}
	}
	return result;
}

std::vector<int16_t> normalise_mono(const mono_t &mono, float gain = 1.0f) {
	float peak = 1e-9f;
	for (const float value : mono) {
		peak = std::max(peak, std::abs(value));
	}
	const float scale = (0.8f * gain) / peak;
	std::vector<int16_t> samples{};
	samples.reserve(mono.size());
	for (const float value : mono) {
		samples.push_back(static_cast<int16_t>(
			std::clamp(value * scale, -1.0f, 1.0f) * 32767.0f));
	}
	return samples;
}

std::shared_ptr<const PcmClip> effect_clip(const mono_t &mono, float gain) {
	auto clip = std::make_shared<PcmClip>();
	auto samples = normalise_mono(mono, gain);
	clip->samples.reserve(samples.size() * OUTPUT_CHANNELS);
	for (const int16_t sample : samples) {
		clip->samples.push_back(sample);
		clip->samples.push_back(sample);
	}
	return clip;
}

void add_u16(std::string &bytes, uint16_t value) {
	bytes.push_back(static_cast<char>(value & 0xff));
	bytes.push_back(static_cast<char>((value >> 8) & 0xff));
}

void add_u32(std::string &bytes, uint32_t value) {
	add_u16(bytes, static_cast<uint16_t>(value & 0xffff));
	add_u16(bytes, static_cast<uint16_t>(value >> 16));
}

std::string music_wav(const mono_t &mono) {
	// A tiny standard mono PCM WAV held in memory for the existing music
	// decoder. This is used only when no real or cached music file exists.
	const auto samples = normalise_mono(mono);
	const auto data_size = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
	std::string bytes{};
	bytes.reserve(44 + data_size);
	bytes.append("RIFF", 4);
	add_u32(bytes, 36 + data_size);
	bytes.append("WAVEfmt ", 8);
	add_u32(bytes, 16);
	add_u16(bytes, 1);  // PCM
	add_u16(bytes, 1);  // mono
	add_u32(bytes, OUTPUT_RATE);
	add_u32(bytes, OUTPUT_RATE * sizeof(int16_t));
	add_u16(bytes, sizeof(int16_t));
	add_u16(bytes, 16);
	bytes.append("data", 4);
	add_u32(bytes, data_size);
	for (const int16_t sample : samples) {
		add_u16(bytes, static_cast<uint16_t>(sample));
	}
	return bytes;
}

} // namespace

SoundBank make_native_placeholder_effects() {
	SoundBank bank{};
	auto add = [&bank](Cue cue, mono_t samples, float gain = 1.0f) {
		bank.add(cue, effect_clip(samples, gain));
	};
	// Short, bright strikes retain a little wood below the metal modes so a
	// melee hit reads at the game's small scale without becoming a bell.
	add(Cue::HIT, mix({metal_impact(0.94f, 0.88f, 1), wood_impact(1.08f, 0.55f, 2)}), 0.91f);
	add(Cue::HIT, mix({metal_impact(1.12f, 0.72f, 3), wood_impact(0.91f, 0.67f, 4)}), 0.83f);
	add(Cue::HIT, mix({metal_impact(0.81f, 1.08f, 5), wood_impact(1.19f, 0.48f, 6)}), 0.88f);
	add(Cue::HIT, mix({metal_impact(1.27f, 0.64f, 7), wood_impact(0.83f, 0.62f, 8)}), 0.76f);
	auto add_arrows = [&add](Cue cue, float pitch, float time_scale, unsigned seed) {
		struct Variation {
			float pitch;
			float duration;
			float brightness;
			float gain;
		};
		// A quick pass can be breathy or sharply whistled at the same pitch.
		constexpr Variation variations[]{
			{1.00f, 1.00f, 1.00f, 1.00f},
			{0.80f, 1.14f, 0.76f, 0.86f},
			{1.17f, 0.80f, 1.28f, 0.92f},
			{0.92f, 0.89f, 1.38f, 0.76f},
			{1.08f, 1.18f, 0.72f, 0.93f},
			{0.88f, 1.03f, 1.10f, 0.97f},
			{1.23f, 0.95f, 0.88f, 0.81f},
		};
		for (const auto &variation : variations) {
			const float shot_pitch = pitch * variation.pitch;
			const float shot_duration = time_scale * variation.duration;
			add(cue, arrow_shot(shot_pitch, shot_duration, variation.brightness, seed++), variation.gain);
		}
	};
	add_arrows(Cue::ARROW_SHOT, 1.06f, 0.92f, 9);
	add_arrows(Cue::TOWER_ARROW_SHOT, 1.0f, 1.0f, 12);

	// The voiced calls are abstract and non-verbal: their changing pitch,
	// formants and breath suggest the event without copying a recording.
	add(Cue::DEATH_HORSE, mix({horse_whinny(0.96f, 1.00f, 0.80f, 27),
	                           concat(silence(0.67f), wood_impact(0.72f, 0.92f, 28))}), 0.88f);
	add(Cue::DEATH_HORSE, mix({horse_whinny(1.09f, 0.86f, 0.61f, 29),
	                           concat(silence(0.54f), wood_impact(0.83f, 0.75f, 30))}), 0.79f);
	add(Cue::DEATH_HORSE, mix({horse_whinny(0.86f, 1.12f, 0.95f, 31),
	                           concat(silence(0.73f), wood_impact(0.67f, 1.04f, 32))}), 0.85f);

	add(Cue::DEATH_BUILDER, builder_fall(1.00f, 1.00f, 0.75f, 33), 0.90f);
	add(Cue::DEATH_BUILDER, builder_fall(0.87f, 0.84f, 0.93f, 35), 0.82f);
	add(Cue::DEATH_BUILDER, builder_fall(1.13f, 1.12f, 0.58f, 37), 0.86f);

	add(Cue::DEATH_TOWER, tower_collapse(1.00f, 39), 0.82f);
	add(Cue::DEATH_TOWER, tower_collapse(0.88f, 47), 0.76f);
	add(Cue::DEATH_TOWER, tower_collapse(1.10f, 55), 0.87f);

	add(Cue::FOUNDATION_PLACED, foundation_placed(1.00f, 63), 0.74f);
	add(Cue::FOUNDATION_PLACED, foundation_placed(0.86f, 68), 0.68f);
	add(Cue::FOUNDATION_PLACED, foundation_placed(1.13f, 73), 0.79f);
	add(Cue::FOUNDATION_PLACED, foundation_placed(0.95f, 78), 0.71f);

	add(Cue::TOWER_BUILT, tower_built(1.00f, 83), 0.72f);
	add(Cue::TOWER_BUILT, tower_built(1.12f, 86), 0.65f);
	add(Cue::TOWER_BUILT, tower_built(0.91f, 89), 0.77f);

	add(Cue::ORDER, order_acknowledgement(1.00f, 92), 0.68f);
	add(Cue::ORDER, order_acknowledgement(1.10f, 94), 0.62f);
	add(Cue::ORDER, order_acknowledgement(0.92f, 96), 0.65f);
	add(Cue::ORDER, order_acknowledgement(1.19f, 98), 0.57f);

	const auto hunter_horn = [](float pitch, float length, unsigned seed) {
		return mix({
			brass_call(305.0f * pitch, 318.0f * pitch, 0.74f * length, 0.13f, seed),
			concat(silence(0.76f * length),
			       brass_call(410.0f * pitch, 397.0f * pitch, 0.96f * length, 0.09f, seed + 1)),
			concat(silence(0.35f * length), noise(1.34f * length, 0.075f, 0.38f * length, seed + 2)),
		});
	};
	add(Cue::HUNTER_UNLEASHED, hunter_horn(1.00f, 1.00f, 101), 0.82f);
	add(Cue::HUNTER_UNLEASHED, hunter_horn(0.91f, 1.10f, 105), 0.76f);

	add(Cue::FLARE, chime(738.0f, 0.64f, 109), 0.68f);
	add(Cue::FLARE, chime(782.0f, 0.57f, 110), 0.63f);
	add(Cue::FLARE, chime(694.0f, 0.72f, 111), 0.71f);
	add(Cue::FLARE, chime(833.0f, 0.52f, 112), 0.58f);
	return bank;
}

MusicTracks make_native_placeholder_music() {
	MusicTracks tracks{};
	auto lobby = sequence({220.0f, 261.6f, 329.6f, 392.0f, 329.6f, 261.6f}, 4, 0.35f);
	for (auto &sample : lobby) {
		sample *= 0.5f;
	}
	tracks.at(static_cast<size_t>(MusicState::LOBBY)).emplace_back(
		"lobby.wav", music_wav(lobby));
	auto pulse = sequence({110.0f, 0.0f, 110.0f, 110.0f, 146.85f, 0.0f, 130.8f, 0.0f}, 4, 0.25f);
	auto drone = tone(55.0f, 55.0f, 8.0f, {1, 0.5f, 0.3f}, 1000.0f);
	for (auto &sample : pulse) {
		sample *= 0.6f;
	}
	for (auto &sample : drone) {
		sample *= 0.25f;
	}
	tracks.at(static_cast<size_t>(MusicState::MATCH)).emplace_back(
		"match.wav", music_wav(mix({pulse, drone})));
	return tracks;
}

namespace {

size_t music_count(const MusicTracks &music) {
	size_t count = 0;
	for (const auto &tracks : music) {
		count += tracks.size();
	}
	return count;
}

/** Read every effect and music file in one sound folder. Never throws. */
void load_sound_folder(const util::Path &sound_dir, SoundBank &bank, MusicTracks &music) {
	util::Path dir = sound_dir;
	try {
		if (not dir.is_dir()) {
			log::log(INFO << "Audio: no sound folder at " << sound_dir);
			return;
		}
		for (auto &entry : dir.iterdir()) {
			const auto &name = entry.get_name();
			const auto cue = cue_for_file(name);
			if (not cue or not entry.is_file()) {
				continue;
			}
			try {
				bank.add(*cue, decode_clip(entry.open_r().read(), name));
			}
			catch (const std::exception &error) {
				log::log(WARN << "Audio: skipping " << name << ": " << error.what());
			}
		}
		auto music_dir = dir / "music";
		if (music_dir.is_dir()) {
			for (auto &entry : music_dir.iterdir()) {
				const auto &name = entry.get_name();
				for (auto state : {MusicState::LOBBY, MusicState::MATCH}) {
					if (is_music_file(name, state) and entry.is_file()) {
						music.at(static_cast<size_t>(state)).emplace_back(name, entry.open_r().read());
					}
				}
			}
		}
	}
	catch (const std::exception &error) {
		log::log(WARN << "Audio: could not read " << sound_dir << ": " << error.what());
	}
}

} // namespace

FvdAudio::FvdAudio(const util::Path &sound_dir,
                   std::shared_ptr<Settings> settings,
                   const std::optional<util::Path> &fallback_dir) :
	settings{std::move(settings)},
	random{std::random_device{}()} {
	for (const auto &rule : RULES) {
		this->throttles.emplace_back(rule.burst, rule.per_second);
	}

	load_sound_folder(sound_dir, this->bank, this->music);

	if (fallback_dir) {
		SoundBank placeholder_bank{};
		MusicTracks placeholder_music{};
		load_sound_folder(*fallback_dir, placeholder_bank, placeholder_music);
		size_t filled = this->bank.fill_missing(placeholder_bank);
		for (size_t state = 0; state < this->music.size(); ++state) {
			if (this->music[state].empty() and not placeholder_music[state].empty()) {
				this->music[state] = std::move(placeholder_music[state]);
				++filled;
			}
		}
		if (filled > 0) {
			log::log(INFO << "Audio: using placeholder sounds from " << *fallback_dir
			              << " for " << filled << " cues without a real file");
		}
	}

	// A native player cannot rely on the Python launcher's cached
	// placeholder generator. Fill only missing cues/music entirely in C++.
	const size_t generated_effects = this->bank.fill_missing(make_native_placeholder_effects());
	size_t generated_music = 0;
	if (this->music.at(static_cast<size_t>(MusicState::LOBBY)).empty()
	    or this->music.at(static_cast<size_t>(MusicState::MATCH)).empty()) {
		auto fallback_music = make_native_placeholder_music();
		for (auto state : {MusicState::LOBBY, MusicState::MATCH}) {
			const auto index = static_cast<size_t>(state);
			if (this->music[index].empty()) {
				this->music[index] = std::move(fallback_music[index]);
				++generated_music;
			}
		}
	}
	if (generated_effects > 0 or generated_music > 0) {
		log::log(INFO << "Audio: synthesised " << generated_effects << " missing effect cues and "
		              << generated_music << " missing music states in C++ (no Python)");
	}

	const size_t music_files = music_count(this->music);
	if (this->bank.total() == 0 and music_files == 0) {
		log::log(INFO << "Audio: no sound files in " << sound_dir << ", playing silently");
		return;
	}
	log::log(INFO << "Audio: loaded " << this->bank.total() << " effects and "
	              << music_files << " music tracks");

	this->mixer = std::make_shared<Mixer>(this->settings);
	this->output = Output::open(this->mixer);
	if (not this->output) {
		this->mixer.reset();
	}
}

FvdAudio::~FvdAudio() = default;

bool FvdAudio::active() const {
	return this->mixer != nullptr;
}

void FvdAudio::set_music(MusicState state) {
	if (state == this->music_state) {
		return;
	}
	this->music_state = state;
	if (not this->active()) {
		return;
	}
	std::unique_ptr<Stream> stream{};
	if (state != MusicState::SILENT) {
		const auto &tracks = this->music.at(static_cast<size_t>(state));
		if (not tracks.empty()) {
			std::uniform_int_distribution<size_t> choose{0, tracks.size() - 1};
			const auto &[name, bytes] = tracks[choose(this->random)];
			try {
				stream = open_stream(bytes, name);
			}
			catch (const std::exception &error) {
				log::log(WARN << "Audio: cannot play " << name << ": " << error.what());
			}
		}
	}
	this->mixer->set_music(std::move(stream), MUSIC_FADE_SECONDS);
}

void FvdAudio::set_view(const Eigen::Matrix4f &view_projection) {
	this->view = view_projection;
}

void FvdAudio::play(Cue cue) {
	this->play_placed(cue, 1.0f, 0.0f);
}

void FvdAudio::play_at(Cue cue, const Eigen::Vector3f &world) {
	if (not this->active() or not this->view) {
		return;
	}
	const auto placement = place_on_screen(world, *this->view);
	if (placement) {
		this->play_placed(cue, placement->gain, placement->pan);
	}
}

void FvdAudio::play_placed(Cue cue, float gain, float pan) {
	if (not this->active() or not this->settings->effects_enabled.load()) {
		return;
	}
	const auto index = static_cast<size_t>(cue);
	auto clip = this->bank.pick(cue, this->random);
	if (not clip or not this->throttles.at(index).allow(Throttle::clock::now())) {
		return;
	}
	this->mixer->play_effect(std::move(clip), gain * RULES.at(index).gain, pan);
}

bool FvdAudio::toggle_mute() {
	const bool muted = not this->settings->muted.load();
	this->settings->muted.store(muted);
	log::log(INFO << "Audio: " << (muted ? "muted" : "unmuted"));
	return muted;
}

} // namespace openage::audio
