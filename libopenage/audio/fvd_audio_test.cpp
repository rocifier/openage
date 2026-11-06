// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <set>

#include "../testing/testing.h"
#include "fvd_audio.h"

namespace openage::audio::tests {

void fvd_audio_files() {
	TESTEQUALS(cue_for_file("hit.opus").value_or(Cue::COUNT), Cue::HIT);
	TESTEQUALS(cue_for_file("hit_2.wav").value_or(Cue::COUNT), Cue::HIT);
	TESTEQUALS(cue_for_file("death_horse_10.opus").value_or(Cue::COUNT), Cue::DEATH_HORSE);
	TESTEQUALS(cue_for_file("tower_built.opus").value_or(Cue::COUNT), Cue::TOWER_BUILT);
	TESTEQUALS(cue_for_file("death_horse.mp3").has_value(), false);
	TESTEQUALS(cue_for_file("hit_x.opus").has_value(), false);
	TESTEQUALS(cue_for_file("README.md").has_value(), false);
	TESTEQUALS(cue_for_file("lobby.opus").has_value(), false);
	for (size_t i = 0; i < CUE_COUNT; ++i) {
		const auto cue = static_cast<Cue>(i);
		TESTEQUALS(cue_for_file(std::string{cue_name(cue)} + ".opus").value_or(Cue::COUNT), cue);
	}

	TESTEQUALS(is_music_file("lobby.opus", MusicState::LOBBY), true);
	TESTEQUALS(is_music_file("lobby_2.wav", MusicState::LOBBY), true);
	TESTEQUALS(is_music_file("match.opus", MusicState::LOBBY), false);
	TESTEQUALS(is_music_file("match.opus", MusicState::MATCH), true);
	TESTEQUALS(is_music_file("match.opus", MusicState::SILENT), false);
}

void fvd_audio_placement() {
	const Eigen::Matrix4f identity = Eigen::Matrix4f::Identity();
	auto centre = place_on_screen({0.0f, 0.0f, 0.0f}, identity);
	TESTEQUALS(centre.has_value(), true);
	TESTEQUALS(centre->gain, 1.0f);
	TESTEQUALS(centre->pan, 0.0f);

	// On screen near an edge: full volume, panned towards it.
	auto left = place_on_screen({-0.9f, 0.5f, 0.0f}, identity);
	TESTEQUALS(left->gain, 1.0f);
	(left->pan < -0.6f) or TESTFAIL;

	// Just beyond the edge fades; further out is culled.
	auto beyond = place_on_screen({1.2f, 0.0f, 0.0f}, identity);
	(std::abs(beyond->gain - 0.6f) < 1e-4f) or TESTFAIL;
	(std::abs(beyond->pan - 0.7f) < 1e-4f) or TESTFAIL;
	TESTEQUALS(place_on_screen({0.0f, 1.6f, 0.0f}, identity).has_value(), false);

	// Behind the camera or degenerate: never placed.
	Eigen::Matrix4f flipped = Eigen::Matrix4f::Identity();
	flipped(3, 3) = -1.0f;
	TESTEQUALS(place_on_screen({0.0f, 0.0f, 0.0f}, flipped).has_value(), false);
	TESTEQUALS(place_on_screen({NAN, 0.0f, 0.0f}, identity).has_value(), false);
}

void fvd_audio_throttle() {
	using namespace std::chrono_literals;
	const auto start = Throttle::clock::time_point{} + 1h;
	Throttle throttle{3.0f, 4.0f};
	TESTEQUALS(throttle.allow(start), true);
	TESTEQUALS(throttle.allow(start), true);
	TESTEQUALS(throttle.allow(start), true);
	TESTEQUALS(throttle.allow(start), false);
	TESTEQUALS(throttle.allow(start + 100ms), false);
	TESTEQUALS(throttle.allow(start + 260ms), true);
	TESTEQUALS(throttle.allow(start + 260ms), false);
	// Refills to the burst, never beyond it.
	TESTEQUALS(throttle.allow(start + 10s), true);
	TESTEQUALS(throttle.allow(start + 10s), true);
	TESTEQUALS(throttle.allow(start + 10s), true);
	TESTEQUALS(throttle.allow(start + 10s), false);
}

void fvd_audio_bank() {
	SoundBank bank{};
	std::minstd_rand random{7};
	TESTEQUALS(bank.pick(Cue::HIT, random) == nullptr, true);

	auto one = std::make_shared<PcmClip>();
	one->samples = {1, 1};
	auto two = std::make_shared<PcmClip>();
	two->samples = {2, 2};
	bank.add(Cue::HIT, one);
	bank.add(Cue::HIT, two);
	bank.add(Cue::HIT, std::make_shared<PcmClip>());
	TESTEQUALS(bank.variants(Cue::HIT), size_t{2});
	TESTEQUALS(bank.total(), size_t{2});

	std::set<const PcmClip *> seen{};
	for (int i = 0; i < 50; ++i) {
		seen.insert(bank.pick(Cue::HIT, random).get());
	}
	TESTEQUALS(seen.size(), size_t{2});
	TESTEQUALS(bank.pick(Cue::ORDER, random) == nullptr, true);

	// Placeholders only fill cues without a real file.
	SoundBank placeholders{};
	auto fake_hit = std::make_shared<PcmClip>();
	fake_hit->samples = {3, 3};
	auto fake_order = std::make_shared<PcmClip>();
	fake_order->samples = {4, 4};
	placeholders.add(Cue::HIT, fake_hit);
	placeholders.add(Cue::ORDER, fake_order);
	TESTEQUALS(bank.fill_missing(placeholders), size_t{1});
	TESTEQUALS(bank.variants(Cue::HIT), size_t{2});
	TESTEQUALS(bank.pick(Cue::ORDER, random) == fake_order, true);
	TESTEQUALS(bank.fill_missing(placeholders), size_t{0});

	// Native executable has no Python generator: every absent effect must
	// have an actual non-silent, playable PCM fallback.
	auto generated = make_native_placeholder_effects();
	TESTEQUALS(generated.variants(Cue::HIT), size_t{2});
	TESTEQUALS(generated.total(), CUE_COUNT + size_t{1});
	for (size_t i = 0; i < CUE_COUNT; ++i) {
		const auto cue = static_cast<Cue>(i);
		TESTEQUALS(generated.variants(cue) > 0, true);
		auto clip = generated.pick(cue, random);
		TESTEQUALS(clip != nullptr, true);
		TESTEQUALS(clip->frames() > 100, true);
		const bool audible = std::any_of(
			clip->samples.begin(), clip->samples.end(), [](int16_t sample) { return sample != 0; });
		TESTEQUALS(audible, true);
	}
	// Real recordings and cached placeholders must not be overridden by
	// the native fallback, even when a generated cue has extra variants.
	TESTEQUALS(bank.fill_missing(generated), CUE_COUNT - size_t{2});
	TESTEQUALS(bank.variants(Cue::HIT), size_t{2});
	TESTEQUALS(bank.variants(Cue::ORDER), size_t{1});
	TESTEQUALS(bank.pick(Cue::ORDER, random) == fake_order, true);

	// A clean native install also has playable music when no real track
	// exists. Validate the generated in-memory WAV through the real decoder.
	const auto music = make_native_placeholder_music();
	for (const auto state : {MusicState::LOBBY, MusicState::MATCH}) {
		const auto &tracks = music.at(static_cast<size_t>(state));
		TESTEQUALS(tracks.size(), size_t{1});
		const auto &track = tracks.front();
		const auto decoded = decode_clip(track.second, track.first);
		TESTEQUALS(decoded != nullptr, true);
		TESTEQUALS(decoded->frames() > OUTPUT_RATE, true);
	}
}

} // namespace openage::audio::tests
