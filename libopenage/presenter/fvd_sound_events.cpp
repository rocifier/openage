// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "fvd_sound_events.h"

#include <unordered_map>

#include "log/log.h"

namespace openage::presenter {

std::optional<audio::Cue> combat_cue(const network::ConfirmedCombatEffect &effect) {
	const bool horse = effect.definition == "fvd.data.entities.Horse";
	const bool builder = effect.definition == "fvd.data.entities.Builder";
	const bool tower = effect.definition == "fvd.data.entities.MedievalTowerLevel1"
	                or effect.definition == "fvd.data.entities.MedievalTowerLevel2";
	if (not effect.death) {
		if (horse or builder or tower) {
			return audio::Cue::HIT;
		}
		return std::nullopt;
	}
	if (horse) {
		return audio::Cue::DEATH_HORSE;
	}
	if (builder) {
		return audio::Cue::DEATH_BUILDER;
	}
	if (tower) {
		return audio::Cue::DEATH_TOWER;
	}
	return std::nullopt;
}

bool hunter_unleashed(int previous_countdown, int countdown) {
	return previous_countdown > 0 and countdown <= 0;
}

std::vector<ConstructionSound> ConstructionSoundTracker::update(
	const network::WorldState &world,
	gamestate::player_id_t local_player,
	bool reset) {
	network::ConstructionPresentation presentation{};
	try {
		presentation = network::make_construction_presentation(world);
	}
	catch (const std::exception &error) {
		// The HUD reports inconsistent worlds; sound just starts over.
		log::log(DBG << "FVD sound: skipped construction diff: " << error.what());
		this->phases.clear();
		this->viewer.reset();
		return {};
	}

	std::unordered_map<gamestate::entity_id_t, const network::EntityState *> entities{};
	for (const auto &entity : world.entities) {
		entities.emplace(entity.entity_id, &entity);
	}

	const bool silent = reset or this->viewer != local_player;
	std::map<site_key_t, network::SitePresentationPhase> next{};
	std::vector<ConstructionSound> sounds{};
	for (const auto &site : presentation.sites) {
		const auto *entity = entities.at(site.entity_id);
		// A fog memory is stale: its real state may have changed unseen.
		if (entity->remembered) {
			continue;
		}
		const site_key_t key{site.owner_id, {site.site.ne, site.site.se}};
		next[key] = site.phase;
		if (silent) {
			continue;
		}
		const auto position = entity->position;
		const auto before = this->phases.find(key);
		const bool complete = site.phase == network::SitePresentationPhase::COMPLETE;
		if (before == this->phases.end()) {
			if (not complete and site.owner_id == local_player) {
				sounds.push_back({audio::Cue::FOUNDATION_PLACED, position, true});
			}
		}
		else if (complete and before->second != network::SitePresentationPhase::COMPLETE) {
			sounds.push_back({audio::Cue::TOWER_BUILT, position, false});
		}
	}
	this->phases = std::move(next);
	this->viewer = local_player;
	return sounds;
}

} // namespace openage::presenter
