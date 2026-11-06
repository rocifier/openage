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

std::vector<int16_t> normalise_mono(const mono_t &mono) {
	float peak = 1e-9f;
	for (const float value : mono) {
		peak = std::max(peak, std::abs(value));
	}
	const float scale = 0.8f / peak;
	std::vector<int16_t> samples{};
	samples.reserve(mono.size());
	for (const float value : mono) {
		samples.push_back(static_cast<int16_t>(
			std::clamp(value * scale, -1.0f, 1.0f) * 32767.0f));
	}
	return samples;
}

std::shared_ptr<const PcmClip> effect_clip(const mono_t &mono) {
	auto clip = std::make_shared<PcmClip>();
	auto samples = normalise_mono(mono);
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
	auto add = [&bank](Cue cue, mono_t samples) {
		bank.add(cue, effect_clip(samples));
	};
	add(Cue::HIT, mix({noise(0.12f, 0.5f, 0.025f, 1),
	                    tone(180, 120, 0.12f, {1.0f}, 0.03f)}));
	add(Cue::HIT, mix({noise(0.12f, 0.4f, 0.03f, 2),
	                    tone(220, 140, 0.12f, {1.0f}, 0.03f)}));
	add(Cue::DEATH_HORSE, mix({tone(520, 160, 0.6f, {1, 0.5f, 0.25f}, 0.2f),
	                           noise(0.6f, 0.1f, 0.15f, 3)}));
	add(Cue::DEATH_BUILDER, mix({tone(300, 90, 0.5f, {1, 0.4f}, 0.15f),
	                             noise(0.3f, 0.3f, 0.05f, 4)}));
	add(Cue::DEATH_TOWER, mix({noise(1.2f, 0.04f, 0.35f, 5),
	                           tone(70, 40, 1.2f, {1.0f}, 0.4f)}));
	add(Cue::FOUNDATION_PLACED, mix({noise(0.08f, 0.6f, 0.01f, 6),
	                                 tone(400, 400, 0.08f, {1.0f}, 0.015f)}));
	add(Cue::TOWER_BUILT, concat(note(523, 0.15f), note(784, 0.35f)));
	add(Cue::ORDER, tone(880, 1100, 0.07f, {1.0f}, 0.02f));
	const auto horn = concat(tone(146.8f, 146.8f, 0.5f, {1, 0.6f, 0.4f, 0.25f}, 0.6f),
	                         tone(110, 98, 1.1f, {1, 0.6f, 0.4f, 0.25f}, 0.5f));
	add(Cue::HUNTER_UNLEASHED, mix({horn,
	                               concat(silence(0.4f), noise(1.2f, 0.05f, 0.4f, 7))}));
	add(Cue::FLARE, mix({tone(1568, 1568, 0.6f, {1, 0.3f, 0.15f}, 0.18f),
	                     concat(silence(0.07f), tone(2093, 2093, 0.55f, {1, 0.3f}, 0.16f))}));
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
