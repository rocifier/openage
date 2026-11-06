// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <eigen3/Eigen/Dense>

#include "../util/path.h"
#include "pcm.h"
#include "settings.h"

namespace openage {
namespace audio {

class Mixer;
class Output;

/** Every FVD sound effect. The file name stem is cue_name(). */
enum class Cue : size_t {
	HIT,
	DEATH_HORSE,
	DEATH_BUILDER,
	DEATH_TOWER,
	FOUNDATION_PLACED,
	TOWER_BUILT,
	ORDER,
	HUNTER_UNLEASHED,
	FLARE,
	COUNT,
};

constexpr size_t CUE_COUNT = static_cast<size_t>(Cue::COUNT);

/** File stem for a cue, e.g. "death_horse" for death_horse.opus / death_horse_2.wav. */
const char *cue_name(Cue cue);

enum class MusicState {
	SILENT,
	LOBBY,
	MATCH,
};

/** Where a world sound sits relative to the screen. */
struct Placement {
	float gain;
	float pan;
};

/**
 * Place a world position for the current camera. Full volume on screen,
 * fading to silence `EDGE_FALLOFF` (in half-screens) beyond the edge.
 * @returns nullopt when the sound should not play at all
 */
std::optional<Placement> place_on_screen(const Eigen::Vector3f &world,
                                         const Eigen::Matrix4f &view_projection);

constexpr float EDGE_FALLOFF = 0.5f;

/**
 * Per-cue rate limit: a burst of up to `capacity`, then `per_second`.
 * Stops 40 tower deaths in one frame from turning into 40 sounds.
 */
class Throttle {
public:
	using clock = std::chrono::steady_clock;

	Throttle(float capacity, float per_second);

	/** Take a token if one is available. */
	bool allow(clock::time_point now);

private:
	float capacity;
	float per_second;
	float tokens;
	std::optional<clock::time_point> last{};
};

/** Raw music files (name, bytes) per MusicState, decoded when a track starts. */
using MusicTracks = std::array<std::vector<std::pair<std::string, std::string>>, 3>;

/** All variants of every cue, picked at random when played. */
class SoundBank {
public:
	void add(Cue cue, std::shared_ptr<const PcmClip> clip);
	std::shared_ptr<const PcmClip> pick(Cue cue, std::minstd_rand &random) const;
	size_t variants(Cue cue) const;
	size_t total() const;

	/**
	 * Copy the variants of every cue that has none here from `other`.
	 * @returns how many cues were filled
	 */
	size_t fill_missing(const SoundBank &other);

private:
	std::array<std::vector<std::shared_ptr<const PcmClip>>, CUE_COUNT> clips{};
};

/** Synthesised temporary FVD effects; real recordings always win. */
SoundBank make_native_placeholder_effects();

/** Native fallbacks for lobby/match music when neither recording nor cache exists. */
MusicTracks make_native_placeholder_music();

/**
 * Which cue a file name belongs to: "<cue>.<ext>" or "<cue>_<n>.<ext>".
 */
std::optional<Cue> cue_for_file(const std::string &file_name);

/** Whether a file is a music track for a state: "<lobby|match>[_n].<ext>". */
bool is_music_file(const std::string &file_name, MusicState state);

/**
 * FVD's sound: lobby/match music and game effects, with throttling and
 * on-screen placement. Without an audio device, sound files or with
 * --no-audio, every call is a cheap no-op.
 *
 * Expected layout under the sound folder:
 *     <cue>[_n].opus|wav    effects, see cue_name()
 *     music/lobby[_n].opus|wav
 *     music/match[_n].opus|wav
 */
class FvdAudio {
public:
	/**
	 * Load sounds and open the default output device.
	 * Never throws: problems are logged and the game stays silent.
	 * Create on the Qt GUI thread.
	 *
	 * @param fallback_dir Same layout; used for each cue and music state
	 *                     that has no file in sound_dir. Holds the generated
	 *                     placeholder sounds until real ones exist.
	 */
	FvdAudio(const util::Path &sound_dir,
	         std::shared_ptr<Settings> settings,
	         const std::optional<util::Path> &fallback_dir = std::nullopt);
	~FvdAudio();

	FvdAudio(const FvdAudio &) = delete;
	FvdAudio &operator=(const FvdAudio &) = delete;

	/** Switch music, crossfading. Repeating the same state does nothing. */
	void set_music(MusicState state);

	/** Camera transform used to place world sounds; call once per frame. */
	void set_view(const Eigen::Matrix4f &view_projection);

	/** A sound for the local player only (not placed in the world). */
	void play(Cue cue);

	/** A sound at a world position (scene world space), placed against the view. */
	void play_at(Cue cue, const Eigen::Vector3f &world);

	/** M key. Returns the new muted state. */
	bool toggle_mute();

	bool active() const;

private:
	void play_placed(Cue cue, float gain, float pan);

	std::shared_ptr<Settings> settings;
	std::shared_ptr<Mixer> mixer;
	std::unique_ptr<Output> output;
	SoundBank bank;
	/** Raw files per music state, decoded when the track starts. */
	MusicTracks music{};
	MusicState music_state = MusicState::SILENT;
	std::vector<Throttle> throttles;
	std::optional<Eigen::Matrix4f> view{};
	std::minstd_rand random;
};

} // namespace audio
} // namespace openage
