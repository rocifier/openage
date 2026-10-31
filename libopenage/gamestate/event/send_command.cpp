// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "send_command.h"

#include <functional>
#include <vector>

#include "coord/phys.h"
#include "error/error.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/idle.h"
#include "gamestate/component/internal/commands/move.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/fvd.h"
#include "network/messages.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/types.h"


namespace openage::gamestate {
namespace component {
class CommandQueue;

namespace command {
class IdleCommand;
class MoveCommand;
} // namespace command
} // namespace component

namespace event {

Commander::Commander(const std::shared_ptr<openage::event::EventLoop> &loop) :
	openage::event::EventEntity{loop} {
}

size_t Commander::id() const {
	return 0;
}

std::string Commander::idstr() const {
	return "command_target";
}


SendCommandHandler::SendCommandHandler() :
	openage::event::OnceEventHandler{"game.send_command"} {
}

void SendCommandHandler::setup_event(const std::shared_ptr<openage::event::Event> & /* event */,
                                     const std::shared_ptr<openage::event::State> & /* state */) {
	// TODO
}

void SendCommandHandler::invoke(openage::event::EventLoop & /* loop */,
                                const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                const std::shared_ptr<openage::event::State> &state,
                                const time::time_t &time,
                                const param_map &params) {
	auto gstate = std::dynamic_pointer_cast<openage::gamestate::GameState>(state);

	auto command_type = params.get("type", component::command::command_t::NONE);
	std::vector<gamestate::entity_id_t> ids = params.get("entity_ids",
	                                                     std::vector<gamestate::entity_id_t>{});
	auto result_cb = params.get("result_cb", std::function<void(bool)>{});
	auto fvd = gstate->get_fvd();
	if (fvd) {
		// Match end is final. Reject even commands already queued locally
		// before the winner was declared; no host-side world mutation.
		if (fvd->get_match_outcome()) {
			if (result_cb) result_cb(false);
			return;
		}
		if (not params.check_type<player_id_t>("owner")) {
			throw Error{ERR << "FVD commands require an authenticated owner"};
		}
		auto owner = params.get<player_id_t>("owner");
		if (command_type == component::command::command_t::BUILD_TOWER) {
			if (not params.check_type<coord::phys3>("target")) {
				throw Error{ERR << "BuildTower requires a target position"};
			}
			const auto position = params.get<coord::phys3>("target", coord::phys3{0, 0, 0});
			const bool append = params.get("queue", false);
			if (params.check_type<std::vector<coord::phys3>>("build_line")) {
				const auto line = params.get<std::vector<coord::phys3>>("build_line");
				bool valid = append and not line.empty()
				          and line.size() <= network::MAX_BUILD_LINE_SITES
				          and line.front() == position;
				if (valid and line.size() > 1) {
					const auto first = line.front().to_tile();
					const auto second = line[1].to_tile();
					const auto ne_step = second.ne - first.ne;
					const auto se_step = second.se - first.se;
					valid = (ne_step == 0 and (se_step == 1 or se_step == -1))
					     or (se_step == 0 and (ne_step == 1 or ne_step == -1));
					for (size_t i = 1; valid and i < line.size(); ++i) {
						const auto previous = line[i - 1].to_tile();
						const auto next = line[i].to_tile();
						valid = next.ne - previous.ne == ne_step
						     and next.se - previous.se == se_step;
					}
				}
				for (const auto &site : line) {
					if (site.up != 0) {
						valid = false;
					}
				}
				bool accepted = false;
				if (valid) {
					for (const auto &site : line) {
						// Invalid or occupied sites are skipped, never reordered.
						accepted = fvd->queue_build(gstate, owner, ids, site, time)
						        or accepted;
					}
				}
				if (result_cb) {
					result_cb(accepted);
				}
				return;
			}
			const bool accepted = append
				? fvd->queue_build(gstate, owner, ids, position, time)
				: fvd->request_build(gstate, owner, ids, position, time);
			if (result_cb) {
				result_cb(accepted);
			}
			return;
		}
		// Right-clicking a visible foundation while commanding a Builder
		// replaces its construction queue rather than issuing a Move.
		// All identity/ownership/queued-site checks stay authoritative.
		if (command_type == component::command::command_t::MOVE
		    and owner >= 2 and params.check_type<entity_id_t>("target_entity")) {
			auto target_id = params.get<entity_id_t>("target_entity");
			auto target = gstate->get_game_entities().find(target_id);
			if (target != gstate->get_game_entities().end()
			    and target->second->get_definition() == "fvd.data.entities.Foundation") {
				const bool accepted = fvd->prioritize_foundation(
					gstate, owner, ids, target_id, time);
				if (result_cb) {
					result_cb(accepted);
				}
				return;
			}
		}
		// An imperfect sprite pick can turn a right-click on a tower into
		// an ordinary ground Move. Resolve the occupied target tile on
		// the authoritative simulation instead of sending horses to melee.
		if (command_type == component::command::command_t::MOVE
		    and owner == 1 and not params.check_type<entity_id_t>("target_entity")
		    and params.check_type<coord::phys3>("target")) {
			auto destination = params.get<coord::phys3>("target", coord::phys3{0, 0, 0});
			if (auto tower = fvd->completed_tower_at(gstate, destination.to_tile())) {
				const bool accepted = fvd->issue_focus_attack(
					gstate, owner, ids, *tower, time);
				if (result_cb) {
					result_cb(accepted);
				}
				return;
			}
		}
		if (command_type == component::command::command_t::FOCUS_ATTACK) {
			const bool accepted = params.check_type<entity_id_t>("target_entity")
			                   and fvd->issue_focus_attack(
				                   gstate, owner, ids,
				                   params.get<entity_id_t>("target_entity"), time);
			if (result_cb) {
				result_cb(accepted);
			}
			return;
		}
		if (command_type == component::command::command_t::ATTACK_MOVE
		    or command_type == component::command::command_t::PATROL) {
			const auto destination = params.get("target", coord::phys3{0, 0, 0});
			const bool accepted = params.check_type<coord::phys3>("target")
			                  and (command_type == component::command::command_t::PATROL
			                           ? fvd->issue_patrol(gstate, owner, ids, destination, time)
			                           : fvd->issue_attack_move(gstate, owner, ids, destination, time));
			if (result_cb) {
				result_cb(accepted);
			}
			return;
		}
		if (command_type == component::command::command_t::GARRISON) {
			const bool accepted = params.check_type<entity_id_t>("target_entity")
			                   and fvd->request_garrison(
			                       gstate, owner, ids,
			                       params.get<entity_id_t>("target_entity"), time);
			if (result_cb) result_cb(accepted);
			return;
		}
		if (command_type == component::command::command_t::UNGARRISON) {
			const bool accepted = fvd->ungarrison_builder(gstate, owner, ids, time);
			if (result_cb) result_cb(accepted);
			return;
		}
		if (command_type == component::command::command_t::SET_RALLY_POINT) {
			const bool accepted = params.check_type<coord::phys3>("target")
			                  and fvd->set_hunter_rally_point(
				                  gstate, owner, ids,
				                  params.get("target", coord::phys3{0, 0, 0}), time);
			if (result_cb) {
				result_cb(accepted);
			}
			return;
		}
		if (command_type == component::command::command_t::DEMOLISH_TOWER) {
			const bool accepted = fvd->demolish_owned_tower(gstate, owner, ids, time);
			if (result_cb) {
				result_cb(accepted);
			}
			return;
		}
		if (command_type == component::command::command_t::SET_STANCE) {
			const bool accepted = params.check_type<HunterStance>("stance")
			                  and fvd->set_hunter_stance(
				                  gstate, owner, ids,
				                  params.get("stance", HunterStance::OBSERVE), time);
			if (result_cb) {
				result_cb(accepted);
			}
			return;
		}
		if ((command_type == component::command::command_t::MOVE
		     or command_type == component::command::command_t::IDLE)
		    and not fvd->can_command_units(gstate, owner, ids, time)) {
			if (result_cb) {
				result_cb(false);
			}
			return;
		}
	}

	bool accepted = false;
	for (auto id : ids) {
		auto found = gstate->get_game_entities().find(id);
		if (fvd and found == gstate->get_game_entities().end()) {
			continue;
		}
		auto entity = fvd ? found->second : gstate->get_game_entity(id);
		if (fvd) {
			if (not entity->has_component(component::component_t::OWNERSHIP)) {
				continue;
			}
			auto ownership = std::dynamic_pointer_cast<component::Ownership>(
				entity->get_component(component::component_t::OWNERSHIP));
			if (ownership->get_owners().get(time) != params.get<player_id_t>("owner")) {
				continue;
			}
			if (command_type != component::command::command_t::MOVE
			    and command_type != component::command::command_t::IDLE) {
				continue;
			}
			if (command_type == component::command::command_t::MOVE
			    and not entity->has_component(component::component_t::MOVE)) {
				continue;
			}
			fvd->cancel_attack_move(id);
			fvd->cancel_pending_garrison(id);
			fvd->cancel_build(gstate, id, time);
		}
		auto command_queue = std::dynamic_pointer_cast<component::CommandQueue>(
			entity->get_component(component::component_t::COMMANDQUEUE));
		std::shared_ptr<component::Activity> activity;
		if (fvd and command_type == component::command::command_t::MOVE) {
			activity = std::dynamic_pointer_cast<component::Activity>(
				entity->get_component(component::component_t::ACTIVITY));
			activity->cancel_events(time);
			while (not command_queue->get_queue().empty(time)) {
				command_queue->pop_command(time);
			}
			activity->init(time);
		}

		switch (command_type) {
		case component::command::command_t::IDLE:
			command_queue->add_command(time, std::make_shared<component::command::IdleCommand>());
			accepted = true;
			break;
		case component::command::command_t::MOVE:
			command_queue->add_command(
				time,
				std::make_shared<component::command::MoveCommand>(
					params.get("target",
			                   coord::phys3{0, 0, 0})));
			accepted = true;
			break;
		default:
			break;
		}
		if (activity) {
			entity->get_manager()->run_activity_system(time);
		}
	}
	if (result_cb) {
		result_cb(accepted);
	}
}

time::time_t SendCommandHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                                     const std::shared_ptr<openage::event::State> & /* state */,
                                                     const time::time_t &at) {
	return at;
}

} // namespace event
} // namespace openage::gamestate
