// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "fvd_sound_events.h"

#include "testing/testing.h"

namespace openage::presenter::tests {

namespace {

network::EntityState site(gamestate::entity_id_t id,
                          gamestate::player_id_t owner,
                          const std::string &kind,
                          int ne,
                          int se) {
	network::EntityState entity{};
	entity.entity_id = id;
	entity.owner_id = owner;
	entity.definition = "fvd.data.entities." + kind;
	entity.position = coord::phys3{coord::phys_t::from_int(ne), coord::phys_t::from_int(se), 0};
	return entity;
}

network::WorldState world_at(int64_t time, std::vector<network::EntityState> entities) {
	network::WorldState world{};
	world.simulation_time = time::time_t::from_int(time);
	world.entities = std::move(entities);
	return world;
}

network::ConfirmedCombatEffect hit(const std::string &kind) {
	return {0, 0, "fvd.data.entities." + kind, coord::phys3{0, 0, 0}, {}, false};
}

} // namespace

void fvd_combat_sounds() {
	auto effect = hit("Horse");
	TESTEQUALS(combat_cue(effect).value_or(audio::Cue::COUNT), audio::Cue::HIT);
	effect.death = true;
	TESTEQUALS(combat_cue(effect).value_or(audio::Cue::COUNT), audio::Cue::DEATH_HORSE);
	effect.definition = "fvd.data.entities.Builder";
	TESTEQUALS(combat_cue(effect).value_or(audio::Cue::COUNT), audio::Cue::DEATH_BUILDER);
	effect.definition = "fvd.data.entities.MedievalTowerLevel1";
	TESTEQUALS(combat_cue(effect).value_or(audio::Cue::COUNT), audio::Cue::DEATH_TOWER);
	effect.definition = "fvd.data.entities.MedievalTowerLevel2";
	TESTEQUALS(combat_cue(effect).value_or(audio::Cue::COUNT), audio::Cue::DEATH_TOWER);
	effect.death = false;
	TESTEQUALS(combat_cue(effect).value_or(audio::Cue::COUNT), audio::Cue::HIT);

	// The Hunter's own tower is not a Builder tower and stays silent.
	effect.definition = "fvd.data.entities.HunterTower";
	TESTEQUALS(combat_cue(effect).has_value(), false);
	effect.death = true;
	TESTEQUALS(combat_cue(effect).has_value(), false);
}

void fvd_hunter_unleashed() {
	TESTEQUALS(hunter_unleashed(1, 0), true);
	TESTEQUALS(hunter_unleashed(14, 0), true); // F9 skips the countdown
	TESTEQUALS(hunter_unleashed(2, 1), false);
	TESTEQUALS(hunter_unleashed(0, 0), false); // joined after the start
	TESTEQUALS(hunter_unleashed(0, 20), false); // new match
}

void fvd_construction_sounds() {
	ConstructionSoundTracker tracker{};
	const auto empty = world_at(1, {});
	const auto planned = world_at(2, {site(10, 2, "Foundation", 5, 5)});
	// The tower may replace the foundation with a new entity ID.
	const auto built = world_at(3, {site(11, 2, "MedievalTowerLevel1", 5, 5)});

	// The first world is only recorded.
	TESTEQUALS(tracker.update(planned, 2, false).size(), size_t{0});
	TESTEQUALS(tracker.update(empty, 2, false).size(), size_t{0});

	// Our own new foundation is feedback for us alone.
	auto sounds = tracker.update(planned, 2, false);
	TESTEQUALS(sounds.size(), size_t{1});
	TESTEQUALS(sounds.front().cue, audio::Cue::FOUNDATION_PLACED);
	TESTEQUALS(sounds.front().local, true);

	// Finishing it is a world sound at the tower.
	sounds = tracker.update(built, 2, false);
	TESTEQUALS(sounds.size(), size_t{1});
	TESTEQUALS(sounds.front().cue, audio::Cue::TOWER_BUILT);
	TESTEQUALS(sounds.front().local, false);
	TESTEQUALS(sounds.front().position.ne, coord::phys_t::from_int(5));
	TESTEQUALS(tracker.update(built, 2, false).size(), size_t{0});

	// Another player's new foundation is not ours to hear; their finished
	// tower is.
	ConstructionSoundTracker ally{};
	ally.update(empty, 3, false);
	TESTEQUALS(ally.update(planned, 3, false).size(), size_t{0});
	sounds = ally.update(built, 3, false);
	TESTEQUALS(sounds.size(), size_t{1});
	TESTEQUALS(sounds.front().cue, audio::Cue::TOWER_BUILT);

	// A tower that simply comes into view was never seen being built.
	ConstructionSoundTracker hunter{};
	hunter.update(empty, 1, false);
	TESTEQUALS(hunter.update(built, 1, false).size(), size_t{0});

	// Fog memories never count, so a revealed tower is silent too.
	auto remembered = planned;
	remembered.entities.front().remembered = true;
	ConstructionSoundTracker fog{};
	fog.update(empty, 1, false);
	fog.update(remembered, 1, false);
	TESTEQUALS(fog.update(built, 1, false).size(), size_t{0});

	// A reset or switching role (solo keys 1-8) is silent.
	ConstructionSoundTracker solo{};
	solo.update(empty, 2, false);
	TESTEQUALS(solo.update(planned, 2, true).size(), size_t{0});
	TESTEQUALS(solo.update(built, 4, false).size(), size_t{0});
}

} // namespace openage::presenter::tests
