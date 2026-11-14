// Copyright 2026 the openage authors. See copying.md for legal info.

#include "game_bindings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>
#include <utility>

#include <QGuiApplication>
#include <QStyleHints>

#include "error/error.h"
#include "log/log.h"

#include "event/event_loop.h"
#include "event/eventhandler.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/fvd.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "input/controller/game/binding.h"
#include "input/controller/game/binding_context.h"
#include "input/controller/game/command_dispatcher.h"
#include "network/entity_kind.h"
#include "renderer/camera/camera.h"
#include "time/clock.h"
#include "time/time_loop.h"
#include "util/vector.h"


namespace openage::input::game {

struct GameBindings::ShiftClick {
	/** Entity under the cursor, regardless of the foundation filter. */
	std::optional<gamestate::entity_id_t> picked;
	/** Owned foundation the click queues, if any. */
	std::optional<gamestate::entity_id_t> foundation;
	gamestate::player_id_t owner;
	CommandRoute route;
};

struct GameBindings::Release {
	/** Camera projection of a drag box at the time of its release. */
	struct Projection {
		Eigen::Matrix4f matrix;
		util::Vector2s viewport;
		Eigen::Vector2f start;
		Eigen::Vector2f end;
	};

	event_arguments args;
	std::optional<coord::input> start;
	std::optional<coord::tile> world_start;
	coord::phys3 mouse_pos;
	int drag_threshold;
	/** Set for drags only. */
	std::optional<Projection> projection;
	/** Set for Shift-clicks only. */
	std::optional<ShiftClick> click;

	bool shift() const {
		return (this->args.e.mod_code & Qt::ShiftModifier) != 0;
	}

	bool dragged() const {
		if (not this->start) {
			return false;
		}
		const auto delta = this->args.mouse - *this->start;
		return std::abs(delta.x) >= this->drag_threshold
		    or std::abs(delta.y) >= this->drag_threshold;
	}
};


namespace {

gamestate::player_id_t player_of(const Controller &controller) {
	return static_cast<gamestate::player_id_t>(controller.get_controlled());
}

void bind_key(BindingContext &ctx,
              int key,
              std::function<void(const std::shared_ptr<Controller> &)> action) {
	ctx.bind(Event{event_class::KEYBOARD, key, Qt::KeyboardModifier::NoModifier, QEvent::KeyRelease},
	         binding_action{forward_action_t::CLEAR,
	                        [action = std::move(action)](const event_arguments &,
	                                                     const std::shared_ptr<Controller> controller) {
		                        action(controller);
	                        }});
}

/** Selection kind for a single selected entity of the given kind. */
SelectionKind selection_kind_of(network::EntityKind kind) {
	switch (kind) {
	case network::EntityKind::BUILDER:
		return SelectionKind::BUILDER;
	case network::EntityKind::HORSE:
		return SelectionKind::HORSE;
	case network::EntityKind::ESCAPE_CART:
		return SelectionKind::CART;
	case network::EntityKind::WOOD_TOWER:
	case network::EntityKind::STONE_TOWER:
		return SelectionKind::TOWER;
	case network::EntityKind::HUNTER_TOWER:
		return SelectionKind::HUNTER_TOWER;
	case network::EntityKind::FOUNDATION:
		return SelectionKind::FOUNDATION;
	default:
		return SelectionKind::OTHER;
	}
}

/** Resolve the selected entity type on the simulation thread. */
void apply_selection(const std::shared_ptr<Controller> &controller,
                     const std::shared_ptr<gamestate::GameState> &state,
                     uint64_t generation,
                     const std::vector<gamestate::entity_id_t> &ids) {
	if (not state->get_fvd()) {
		controller->set_selected_if_current(generation, ids);
		return;
	}
	auto is_horse = [&](gamestate::entity_id_t id) {
		auto found = state->get_game_entities().find(id);
		return found != state->get_game_entities().end()
		    and network::classify(found->second->get_definition()) == network::EntityKind::HORSE;
	};
	auto is_unit = [&](gamestate::entity_id_t id) {
		auto found = state->get_game_entities().find(id);
		return found != state->get_game_entities().end()
		    and network::is_unit(network::classify(found->second->get_definition()));
	};
	if (ids.size() > 1) {
		const auto chosen = units_take_priority(ids, is_unit);
		if (chosen.size() > 1) {
			bool all_horses = std::all_of(chosen.begin(), chosen.end(), is_horse);
			controller->set_selected_if_current(
				generation, chosen,
				all_horses ? SelectionKind::GROUP_HORSES : SelectionKind::NONE);
			return;
		}
		// One unit and some buildings: select that unit on its own below.
		apply_selection(controller, state, generation, chosen);
		return;
	}
	if (ids.empty()) {
		controller->set_selected_if_current(generation, {});
		return;
	}
	auto found = state->get_game_entities().find(ids.front());
	if (found == state->get_game_entities().end()) {
		controller->set_selected_if_current(generation, {});
		return;
	}
	const auto kind = selection_kind_of(network::classify(found->second->get_definition()));
	controller->set_selected_if_current(
		generation, ids, kind, found->second->get_render_entity());
}

/** Structure picks use authoritative recipient-visible snapshots, independent
 * of the host-local game.click_select ownership-only event. */
bool apply_structure_click(
	const std::shared_ptr<Controller> &controller,
	const CommandDispatcher &commands,
	const std::shared_ptr<renderer::camera::Camera> &camera,
	gamestate::entity_id_t clicked, uint64_t generation,
	const SelectionDetails &previous, bool shift, bool double_click) {
	const auto snapshot = commands.snapshot();
	const network::WorldState *world = snapshot.world.get();
	if (not world) return false;
	const auto viewer = player_of(*controller);
	const auto on_screen = [camera](const network::EntityState &entity) {
		const Eigen::Matrix4f matrix = camera->get_projection_matrix() * camera->get_view_matrix();
		const auto pos = entity.position.to_scene3().to_world_space();
		const Eigen::Vector4f clip = matrix * Eigen::Vector4f{pos.x(), pos.y(), pos.z(), 1.0f};
		return std::isfinite(clip.w()) and clip.w() > 0.0f
		    and std::isfinite(clip.x()) and std::isfinite(clip.y())
		    and std::abs(clip.x() / clip.w()) <= 1.0f
		    and std::abs(clip.y() / clip.w()) <= 1.0f;
	};
	const auto car = std::find_if(world->entities.begin(), world->entities.end(),
	    [clicked](const network::EntityState &entry) { return entry.entity_id == clicked; });
	if (viewer >= 2 and car != world->entities.end() and not car->remembered
	    and car->owner_id >= 2 and car->kind() == network::EntityKind::ESCAPE_CART) {
		controller->set_selected_if_current(generation, {clicked}, SelectionKind::CART);
		return true;
	}
	const auto result = select_fvd_structure(
		*world, viewer, clicked, previous, shift, double_click, on_screen);
	if (not result) return false;
	controller->set_selected_if_current(generation, result->ids, result->kind);
	return true;
}

/** Resolve guest selections exclusively from the host's replicated world. */
void apply_guest_selection(
	const std::shared_ptr<Controller> &controller,
	const network::WorldState &world,
	uint64_t generation,
	const std::vector<gamestate::entity_id_t> &ids) {
	std::vector<gamestate::entity_id_t> valid;
	SelectionKind kind = SelectionKind::NONE;
	auto find = [&world](gamestate::entity_id_t id) {
		return std::find_if(world.entities.begin(), world.entities.end(),
			[id](const network::EntityState &entity) { return entity.entity_id == id; });
	};
	const auto candidates = units_take_priority(ids, [&](gamestate::entity_id_t id) {
		auto found = find(id);
		return found != world.entities.end() and found->owner_id == world.local_player_id
		    and network::is_unit(found->kind());
	});
	for (auto id : candidates) {
		auto found = find(id);
		if (found == world.entities.end()) {
			continue;
		}
		const bool own = found->owner_id == world.local_player_id;
		const bool allied_tower = world.local_player_id >= 2
		                       and found->owner_id >= 2
		                       and (network::is_tower(found->kind())
		                            or found->kind() == network::EntityKind::ESCAPE_CART);
		if (not own and not allied_tower) {
			continue;
		}
		const auto entity_selection = selection_kind_of(found->kind());
		if (entity_selection == SelectionKind::OTHER) {
			continue;
		}
		// Horses form groups; any other kind is selected on its own.
		if (entity_selection == SelectionKind::HORSE) {
			if (kind != SelectionKind::NONE and kind != SelectionKind::HORSE) {
				continue;
			}
		}
		else if (not valid.empty()) {
			continue;
		}
		kind = entity_selection;
		valid.push_back(id);
		if (kind != SelectionKind::HORSE) break;
	}
	if (kind == SelectionKind::HORSE and valid.size() > 1) {
		kind = SelectionKind::GROUP_HORSES;
	}
	controller->set_selected_if_current(generation, valid, kind);
}

} // namespace


GameBindings::GameBindings(std::shared_ptr<time::TimeLoop> time_loop,
                           std::shared_ptr<gamestate::GameSimulation> simulation,
                           std::shared_ptr<renderer::camera::Camera> camera,
                           entity_picker_t pick_entity,
                           std::shared_ptr<const CommandDispatcher> commands,
                           std::function<bool()> can_spawn_hunter) :
	time_loop{std::move(time_loop)},
	simulation{std::move(simulation)},
	camera{std::move(camera)},
	pick_entity{std::move(pick_entity)},
	commands{std::move(commands)},
	can_spawn_hunter{std::move(can_spawn_hunter)} {
	if (not this->time_loop or not this->simulation or not this->camera
	    or not this->pick_entity or not this->commands) {
		throw Error{MSG(err) << "Game bindings need a time loop, simulation, camera, "
		                     << "entity picker and command dispatcher"};
	}
}

void GameBindings::bind(const std::shared_ptr<BindingContext> &ctx,
                        const std::shared_ptr<Controller> &controller,
                        bool online_lobby) {
	const auto self = this->shared_from_this();
	using handler_t = void (GameBindings::*)(const event_arguments &, const controller_t &) const;
	const auto handle = [self](handler_t handler) {
		return binding_func_t{[self, handler](const event_arguments &args,
		                                      const std::shared_ptr<Controller> controller) {
			std::invoke(handler, *self, args, controller);
		}};
	};

	const auto fvd = this->simulation->get_game()->get_state()->get_fvd();
	if (fvd) {
		bind_key(*ctx, Qt::Key_Delete, [self](const controller_t &ctl) {
			self->demolish_selected(ctl);
		});
		bind_key(*ctx, Qt::Key_V, [self](const controller_t &ctl) {
			const auto selection = ctl->get_selection_details();
			if (ctl->get_controlled() < 2 or selection.kind != SelectionKind::BUILDER
			    or selection.ids.size() != 1) return;
			self->commands->dispatch(
				network::CommandIntent{0, network::CommandKind::BUILD_CART, selection.ids},
				player_of(*ctl));
		});
		bind_key(*ctx, Qt::Key_T, [](const controller_t &ctl) {
			ctl->begin_garrison_mode();
		});
		bind_key(*ctx, Qt::Key_G, [self](const controller_t &ctl) {
			self->ungarrison(ctl);
		});
		std::weak_ptr<Controller> weak_controller = controller;
		controller->set_hunter_stance_action([self, weak_controller](gamestate::HunterStance stance) {
			if (auto active = weak_controller.lock()) {
				self->set_stance(active, stance);
			}
		});
		controller->set_ungarrison_action([self, weak_controller]() {
			if (auto active = weak_controller.lock()) {
				self->ungarrison(active);
			}
		});

		bind_key(*ctx, Qt::Key_A, [](const controller_t &ctl) {
			ctl->begin_attack_move();
		});
		bind_key(*ctx, Qt::Key_P, [](const controller_t &ctl) {
			ctl->begin_patrol();
		});
		for (const auto &entry : {std::pair{Qt::Key_Z, gamestate::HunterStance::AGGRESSIVE},
		                          std::pair{Qt::Key_X, gamestate::HunterStance::DEFENSIVE},
		                          std::pair{Qt::Key_C, gamestate::HunterStance::OBSERVE}}) {
			const auto stance = entry.second;
			bind_key(*ctx, entry.first, [stance](const controller_t &ctl) {
				ctl->issue_hunter_stance(stance);
			});
		}
		bind_key(*ctx, Qt::Key_B, [](const controller_t &ctl) {
			ctl->begin_tower_placement();
		});
		bind_key(*ctx, Qt::Key_Escape, [](const controller_t &ctl) {
			ctl->cancel_tower_placement();
			ctl->cancel_garrison_mode();
			ctl->cancel_hunter_targeting();
		});
		bind_key(*ctx, Qt::Key_F9, [self](const controller_t &) {
			self->spawn_hunter();
		});

		if (not online_lobby) {
			for (size_t id = 1; id <= fvd->get_player_count(); ++id) {
				bind_key(*ctx, static_cast<int>(Qt::Key_0 + id), [self, fvd, id](const controller_t &ctl) {
					self->switch_player(*fvd, id, ctl);
				});
			}
		}
	}

	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::RightButton,
	                Qt::KeyboardModifier::NoModifier, QEvent::MouseButtonRelease},
	          binding_action{forward_action_t::SEND, handle(&GameBindings::issue_order)});
	// Shift+right-click queues an existing foundation without entering
	// placement mode, matching the AoE2 queued-order gesture.
	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::RightButton,
	                Qt::KeyboardModifier::ShiftModifier, QEvent::MouseButtonRelease},
	          binding_action{forward_action_t::SEND, handle(&GameBindings::queue_foundation)});

	const binding_action init_drag_action{forward_action_t::CLEAR, handle(&GameBindings::init_drag)};
	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::NoModifier, QEvent::MouseButtonPress},
	          init_drag_action);
	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::ShiftModifier, QEvent::MouseButtonPress},
	          init_drag_action);
	ctx->bind(Event{event_class::MOUSE_BUTTON_DBL, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::ShiftModifier, QEvent::MouseButtonDblClick},
	          init_drag_action);

	// Shift+left release queues a click or a constrained multi-site drag.
	const binding_action left_release_action{forward_action_t::CLEAR, handle(&GameBindings::left_release)};
	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::NoModifier, QEvent::MouseButtonRelease},
	          left_release_action);
	ctx->bind(Event{event_class::MOUSE_BUTTON, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::ShiftModifier, QEvent::MouseButtonRelease},
	          left_release_action);

	// Qt delivers a final left release after its double-click event. Process
	// the group here and suppress that release so it cannot undo the group.
	ctx->bind(Event{event_class::MOUSE_BUTTON_DBL, Qt::MouseButton::LeftButton,
	                Qt::KeyboardModifier::NoModifier, QEvent::MouseButtonDblClick},
	          binding_action{forward_action_t::CLEAR, handle(&GameBindings::select_structure_group)});
}

void GameBindings::pick(const coord::input &position, const entity_pick_callback_t &callback) const {
	this->pick_entity(position, callback, {});
}

void GameBindings::place_tower(const coord::phys3 &position,
                               bool append,
                               const controller_t &controller) const {
	auto state = this->simulation->get_game()->get_state();
	if (state->get_fvd()) {
		auto selection = controller->get_selection_details();
		if (selection.kind != SelectionKind::BUILDER) {
			return;
		}
		const auto owner = player_of(*controller);
		const auto route = this->commands->route();
		// Stay in placement mode so an invalid click is correctable.
		if (route == CommandRoute::LOCAL
		    and not state->get_fvd()->can_place_tower(
				state, owner, selection.ids, position, this->time_loop->get_clock()->get_time())) {
			return;
		}
		network::CommandIntent intent{
			0, network::CommandKind::BUILD_TOWER, selection.ids, position};
		intent.queue = append;
		if (this->commands->dispatch(route, intent, owner) and not append) {
			controller->cancel_tower_placement();
		}
		return;
	}

	event::EventHandler::param_map::map_t params{
		{"position", position},
		{"owner", controller->get_controlled()},
		{"entity_ids", controller->get_selected()},
	};
	this->simulation->get_event_loop()->submit_event(
		"game.spawn_entity", this->simulation->get_spawner(), state,
		this->time_loop->get_clock()->get_time(), params);
}

void GameBindings::issue_hunter_route(const event_arguments &args,
                                      const controller_t &controller) const {
	const bool patrol = controller->is_patrol_active();
	if (not patrol and not controller->is_attack_move_active()) {
		return;
	}
	controller->cancel_hunter_targeting();
	controller->notify_order_issued();
	this->commands->dispatch(
		network::CommandIntent{
			0, patrol ? network::CommandKind::PATROL : network::CommandKind::ATTACK_MOVE,
			controller->get_selected(), args.mouse.to_phys3(this->camera)},
		player_of(*controller));
}

void GameBindings::set_stance(const controller_t &controller, gamestate::HunterStance stance) const {
	if (not controller->is_hunter_selection()) {
		return;
	}
	network::CommandIntent intent{
		0, network::CommandKind::SET_STANCE, controller->get_selected()};
	intent.stance = stance;
	this->commands->dispatch(intent, player_of(*controller));
}

void GameBindings::demolish_selected(const controller_t &controller) const {
	const auto selection = controller->get_selection_details();
	const auto owner = player_of(*controller);
	// Delete removes the selected tower (with its collapse animation)
	// or the selected foundation; the host checks ownership.
	if (owner < 2 or selection.ids.size() != 1
	    or (selection.kind != SelectionKind::TOWER and selection.kind != SelectionKind::FOUNDATION)) {
		return;
	}
	this->commands->dispatch(
		network::CommandIntent{0, network::CommandKind::DEMOLISH_TOWER, selection.ids},
		owner);
}

void GameBindings::ungarrison(const controller_t &controller) const {
	const auto selection = controller->get_selection_details();
	const auto owner = player_of(*controller);
	if (owner < 2 or selection.ids.size() != 1
	    or (selection.kind != SelectionKind::TOWER
	        and selection.kind != SelectionKind::CART)) {
		return;
	}
	this->commands->dispatch(
		network::CommandIntent{0, network::CommandKind::UNGARRISON, selection.ids},
		owner);
}

void GameBindings::spawn_hunter() const {
	if (this->can_spawn_hunter and not this->can_spawn_hunter()) {
		return;
	}
	auto state = this->simulation->get_game()->get_state();
	auto fvd = state->get_fvd();
	if (not fvd) {
		throw Error{MSG(err) << "F9 Hunter spawn requested outside an FVD game"};
	}
	fvd->request_immediate_hunter_spawn(state, this->time_loop->get_clock()->get_time());
}

void GameBindings::switch_player(const gamestate::FVD &fvd,
                                 size_t id,
                                 const controller_t &controller) const {
	controller->set_control(id);
	this->camera->look_at_coord(fvd.get_centre().to_scene3());
	log::log(INFO << "Controlling player " << id << (id == 1 ? " (Hunter)" : " (Builders)"));
}

void GameBindings::issue_order(const event_arguments &args, const controller_t &controller) const {
	if (controller->defer_until_selection_applied(
			[self = this->shared_from_this(), args, controller]() {
				self->submit_order(args, controller);
			})) {
		return;
	}
	this->submit_order(args, controller);
}

void GameBindings::submit_order(const event_arguments &args, const controller_t &controller) const {
	if (controller->is_garrison_mode_active()) {
		controller->cancel_garrison_mode();
		return;
	}
	if (controller->is_tower_placement_active()) {
		controller->cancel_tower_placement();
		return;
	}
	if (controller->is_attack_move_active() or controller->is_patrol_active()) {
		controller->cancel_hunter_targeting();
		return;
	}
	const auto selection = controller->get_selection_details();
	const auto owner = player_of(*controller);
	if (owner >= 2 and selection.kind == SelectionKind::CART
	    and selection.ids.size() == 1) {
		// Passengers can select allied carts to disembark, but only the
		// cart owner may send movement orders.
		const auto snapshot = this->commands->snapshot();
		const bool owns_cart = snapshot.world and std::any_of(
			snapshot.world->entities.begin(), snapshot.world->entities.end(),
			[&](const network::EntityState &entity) {
				return entity.entity_id == selection.ids.front()
				    and entity.kind() == network::EntityKind::ESCAPE_CART
				    and entity.owner_id == owner;
			});
		if (owns_cart)
			this->commands->dispatch(network::CommandIntent{
				0, network::CommandKind::MOVE, selection.ids, args.mouse.to_phys3(this->camera)}, owner);
		return;
	}
	if (owner >= 2 and selection.kind == SelectionKind::TOWER
	    and selection.ids.size() == 1) {
		this->set_ungarrison_point(args, selection, owner);
		return;
	}
	// Right-clicking with the Hunter tower selected moves its rally point.
	if (owner == 1 and selection.kind == SelectionKind::HUNTER_TOWER
	    and selection.ids.size() == 1) {
		this->commands->dispatch(
			network::CommandIntent{
				0, network::CommandKind::SET_RALLY_POINT, selection.ids, args.mouse.to_phys3(this->camera)},
			owner);
		return;
	}
	// Acknowledge orders to the player's own units, as AoE2 does.
	if (not selection.ids.empty()
	    and ((owner == 1 and (selection.kind == SelectionKind::HORSE
	                          or selection.kind == SelectionKind::GROUP_HORSES))
	         or (owner >= 2 and selection.kind == SelectionKind::BUILDER))) {
		controller->notify_order_issued();
	}
	if (auto replica = this->commands->guest_world()) {
		this->submit_guest_order(args, selection, owner, std::move(replica));
		return;
	}
	this->submit_local_order(args, selection, owner);
}

void GameBindings::set_ungarrison_point(const event_arguments &args,
                                        const SelectionDetails &selection,
                                        gamestate::player_id_t owner) const {
	// Treat right-clicking another owned tower as an ENTITY target, not
	// the ground underneath its sprite. Preserve the tower ID across the
	// network so a raised red arrow cannot affect movement or hopping.
	const auto mouse_pos = args.mouse.to_phys3(this->camera);
	this->pick(args.mouse, [commands = this->commands, ids = selection.ids, mouse_pos, owner](
							   std::optional<gamestate::entity_id_t> picked) {
		const auto snapshot = commands->snapshot();
		const auto tower = friendly_tower_target(
			snapshot.world.get(), owner, picked, mouse_pos.to_tile(), ids.front());
		network::CommandIntent intent{
			0, network::CommandKind::SET_UNGARRISON_POINT, ids, mouse_pos};
		if (tower) {
			intent.target_entity = tower->id;
			if (tower->position) {
				intent.target = *tower->position;
				intent.target.up = coord::phys_t::from_int(0);
			}
		}
		commands->dispatch(snapshot.route, intent, owner);
	});
}

void GameBindings::submit_guest_order(const event_arguments &args,
                                      const SelectionDetails &selection,
                                      gamestate::player_id_t owner,
                                      std::shared_ptr<const network::WorldState> replica) const {
	if (selection.ids.empty()) {
		return;
	}
	const auto mouse_pos = args.mouse.to_phys3(this->camera);
	const bool horses = owner == 1
	                and (selection.kind == SelectionKind::HORSE
	                     or selection.kind == SelectionKind::GROUP_HORSES);
	const bool builder = owner >= 2 and selection.kind == SelectionKind::BUILDER;
	auto order = [commands = this->commands, ids = selection.ids, mouse_pos,
	              replica = std::move(replica), owner, horses, builder](
					 std::optional<gamestate::entity_id_t> hit) {
		network::CommandIntent intent{
			0, network::CommandKind::MOVE, ids, mouse_pos};
		if (hit) {
			auto found = std::find_if(
				replica->entities.begin(), replica->entities.end(),
				[hit](const network::EntityState &entry) {
					return entry.entity_id == *hit;
				});
			if (found != replica->entities.end()) {
				if (horses and found->owner_id != owner
				    and (found->kind() == network::EntityKind::BUILDER
				         or network::is_tower(found->kind()))) {
					intent.kind = network::CommandKind::FOCUS_ATTACK;
					intent.target_entity = *hit;
				}
				else if (builder and found->owner_id == owner
				         and found->kind() == network::EntityKind::FOUNDATION) {
					intent.kind = network::CommandKind::PRIORITIZE_FOUNDATION;
					intent.target_entity = *hit;
				}
			}
		}
		commands->dispatch(CommandRoute::GUEST, intent, owner);
	};
	if (horses or builder) {
		this->pick(args.mouse, order);
	}
	else {
		order(std::nullopt);
	}
}

void GameBindings::submit_local_order(const event_arguments &args,
                                      const SelectionDetails &selection,
                                      gamestate::player_id_t owner) const {
	const auto mouse_pos = args.mouse.to_phys3(this->camera);
	// The render-thread pick reports only an entity ID. Target
	// ownership, type, and life are checked on the simulation thread.
	const bool hunter_selection = owner == 1 and not selection.ids.empty()
	                          and (selection.kind == SelectionKind::HORSE
	                               or selection.kind == SelectionKind::GROUP_HORSES);
	const bool builder_selection = owner >= 2 and selection.ids.size() == 1
	                           and selection.kind == SelectionKind::BUILDER;
	auto send_order = [commands = this->commands, mouse_pos, owner, ids = selection.ids,
	                   hunter_selection, builder_selection](
						  std::optional<gamestate::entity_id_t> picked) {
		network::CommandIntent intent{
			0,
			picked and hunter_selection ? network::CommandKind::FOCUS_ATTACK
			                            : network::CommandKind::MOVE,
			ids, mouse_pos};
		if (picked and (hunter_selection or builder_selection)) {
			intent.target_entity = *picked;
		}
		commands->dispatch(CommandRoute::LOCAL, intent, owner);
	};
	if (hunter_selection or builder_selection) {
		this->pick(args.mouse, send_order);
	}
	else {
		send_order(std::nullopt);
	}
}

void GameBindings::resolve_queue_click(const coord::input &position,
                                       const coord::phys3 &mouse_pos,
                                       gamestate::player_id_t owner,
                                       std::function<void(const ShiftClick &)> callback) const {
	const auto snapshot = this->commands->snapshot();
	const auto &world = snapshot.world;
	const auto route = snapshot.route;
	this->pick(position, [self = this->shared_from_this(), callback = std::move(callback),
	                      world, route, owner, mouse_pos, position](
							 std::optional<gamestate::entity_id_t> picked) {
		auto finish = [callback, world, route, owner, mouse_pos, picked](
						  std::optional<gamestate::entity_id_t> hit) {
			const auto foundation = world
			                          ? queue_foundation_target(*world, owner, hit, mouse_pos.to_tile())
			                          : std::nullopt;
			callback(ShiftClick{picked, foundation, owner, route});
		};
		std::unordered_set<gamestate::entity_id_t> foundations;
		if (world and owner >= 2) {
			for (const auto &entity : world->entities) {
				if (entity.owner_id == owner and not entity.remembered
				    and entity.kind() == network::EntityKind::FOUNDATION) {
					foundations.insert(entity.entity_id);
				}
			}
		}
		// Queue targeting must not stop at an overlapping tower or unit.
		// Retain the unfiltered pick for non-Builder selection handling.
		if (not foundations.empty()) {
			self->pick_entity(position, finish,
			                  [foundations = std::move(foundations)](gamestate::entity_id_t id) {
				                  return foundations.contains(id);
			                  });
		}
		else {
			finish(picked);
		}
	});
}

void GameBindings::queue_existing_foundation(const std::vector<gamestate::entity_id_t> &ids,
                                             const ShiftClick &click) const {
	if (not click.foundation) {
		throw Error{ERR << "Foundation queue click has no foundation target"};
	}
	network::CommandIntent intent{0, network::CommandKind::PRIORITIZE_FOUNDATION, ids};
	intent.target_entity = *click.foundation;
	intent.queue = true;
	this->commands->dispatch(click.route, intent, click.owner, [](bool accepted) {
		if (not accepted) {
			log::log(WARN << "FVD foundation queue rejected: invalid Builder, unavailable site or queue full");
		}
	});
}

void GameBindings::queue_foundation(const event_arguments &args, const controller_t &controller) const {
	const auto owner = player_of(*controller);
	if (owner < 2) {
		return;
	}
	const auto mouse_pos = args.mouse.to_phys3(this->camera);
	std::weak_ptr<Controller> weak_controller = controller;
	this->resolve_queue_click(
		args.mouse, mouse_pos, owner,
		[self = this->shared_from_this(), weak_controller](const ShiftClick &click) {
			if (not click.foundation) {
				return;
			}
			auto controller = weak_controller.lock();
			if (not controller) {
				return;
			}
			auto action = [self, weak_controller, click]() {
				auto active = weak_controller.lock();
				if (not active or active->get_controlled() != click.owner) {
					return;
				}
				const auto selection = active->get_selection_details();
				if (selection.ids.size() != 1 or selection.kind != SelectionKind::BUILDER) {
					return;
				}
				self->queue_existing_foundation(selection.ids, click);
			};
			if (controller->defer_until_selection_applied(action)) {
				return;
			}
			action();
		});
}

void GameBindings::init_drag(const event_arguments &args, const controller_t &controller) const {
	controller->set_drag_select_start(args.mouse, args.mouse.to_phys3(this->camera).to_tile());
}

void GameBindings::left_release(const event_arguments &args, const controller_t &controller) const {
	if (controller->consume_structure_double_click_release()) {
		controller->reset_drag_select();
		return;
	}
	Release release{args,
	                controller->get_drag_select_start_optional(),
	                controller->get_drag_world_start_optional(),
	                args.mouse.to_phys3(this->camera),
	                QGuiApplication::styleHints()->startDragDistance(),
	                std::nullopt,
	                std::nullopt};
	controller->reset_drag_select();
	if (release.dragged()) {
		release.projection = Release::Projection{
			this->camera->get_projection_matrix() * this->camera->get_view_matrix(),
			this->camera->get_viewport_size(),
			release.start->to_viewport(this->camera).to_ndc_space(this->camera),
			args.mouse.to_viewport(this->camera).to_ndc_space(this->camera)};
	}
	const bool shift = release.shift();
	auto dispatch = [self = this->shared_from_this(), release, controller, shift](
						std::optional<ShiftClick> click) {
		Release clicked = release;
		clicked.click = std::move(click);
		std::weak_ptr<Controller> weak_controller = controller;
		auto action = [self, clicked = std::move(clicked), weak_controller]() {
			if (auto active = weak_controller.lock()) {
				self->process_release(clicked, active);
			}
		};
		if (shift and controller->defer_until_selection_applied(action)) {
			return;
		}
		action();
	};
	if (not shift or (release.dragged() and release.world_start)) {
		dispatch(std::nullopt);
		return;
	}
	this->resolve_queue_click(args.mouse, release.mouse_pos, player_of(*controller),
	                          [dispatch](const ShiftClick &click) { dispatch(click); });
}

void GameBindings::process_release(const Release &release, const controller_t &controller) const {
	const bool shift = release.shift();
	const auto selection = controller->get_selection_details();
	if (controller->is_garrison_mode_active()) {
		this->garrison(release, controller, selection);
		return;
	}
	// Queueing an existing foundation leaves placement mode as it was.
	if (shift and release.click and release.click->foundation
	    and selection.kind == SelectionKind::BUILDER) {
		if (controller->get_controlled() == release.click->owner) {
			this->queue_existing_foundation(selection.ids, *release.click);
		}
		return;
	}
	if (shift and selection.kind == SelectionKind::BUILDER
	    and controller->begin_tower_placement()) {
		this->build_shift_click(release, controller, selection);
		return;
	}
	if (controller->is_tower_placement_active()) {
		this->place_tower(release.mouse_pos, shift, controller);
		return;
	}
	if (controller->is_attack_move_active() or controller->is_patrol_active()) {
		this->issue_hunter_route(release.args, controller);
		return;
	}
	const auto generation = controller->begin_selection_update();
	if (not release.start) {
		throw Error{ERR << "Drag select start not set."};
	}
	if (auto replica = this->commands->guest_world()) {
		this->select_guest(release, controller, selection, generation, std::move(replica));
		return;
	}
	this->select_local(release, controller, selection, generation);
}

void GameBindings::pick_at_click(const Release &release, const entity_pick_callback_t &callback) const {
	if (release.click) {
		callback(release.click->picked);
	}
	else {
		this->pick(release.args.mouse, callback);
	}
}

void GameBindings::garrison(const Release &release,
                            const controller_t &controller,
                            const SelectionDetails &selection) const {
	if (selection.kind != SelectionKind::BUILDER or selection.ids.size() != 1) {
		controller->cancel_garrison_mode();
		return;
	}
	const auto owner = player_of(*controller);
	const auto clicked_tile = release.mouse_pos.to_tile();
	// A thin tower silhouette may not produce a sprite pick.
	// Resolve the tile from the owner's authoritative presentation
	// snapshot instead, without relying on paused guest simulation.
	this->pick_at_click(release, [commands = this->commands, controller, selection, owner, clicked_tile](
									 std::optional<gamestate::entity_id_t> picked) {
		if (not controller->is_garrison_mode_active()
		    or controller->get_selected() != selection.ids) {
			return;
		}
		const auto snapshot = commands->snapshot();
		// Without a snapshot the pick is sent unchecked; the
		// simulation validates the target's type and owner.
		std::optional<gamestate::entity_id_t> cart_id;
		if (snapshot.world) {
			for (const auto &entry : snapshot.world->entities) {
				if (entry.kind() != network::EntityKind::ESCAPE_CART
				    or entry.remembered or entry.owner_id < 2) continue;
				if ((picked and entry.entity_id == *picked)
				    or (not picked and entry.position.to_tile() == clicked_tile)) {
					cart_id = entry.entity_id;
					break;
				}
			}
		}
		std::optional<TowerTarget> tower;
		if (not cart_id)
			tower = friendly_tower_target(snapshot.world.get(), owner, picked, clicked_tile);
		if (not cart_id and not tower) {
			log::log(INFO << "FVD garrison: click a friendly completed tower");
			return; // Keep T mode armed for another click or Esc.
		}
		controller->cancel_garrison_mode();
		network::CommandIntent intent{0, network::CommandKind::GARRISON, selection.ids};
		intent.target_entity = cart_id ? *cart_id : tower->id;
		commands->dispatch(snapshot.route, intent, owner, [](bool accepted) {
			if (not accepted) {
				log::log(WARN << "FVD garrison rejected: tower must be finished, empty and reachable");
			}
		});
	});
}

void GameBindings::build_shift_click(const Release &release,
                                     const controller_t &controller,
                                     const SelectionDetails &selection) const {
	if (release.dragged() and release.world_start) {
		const auto sites = tower_line_sites(*release.world_start, release.mouse_pos.to_tile());
		// A single authoritative event preserves the line order even
		// when equal-time events execute newest-first.
		if (sites.size() > network::MAX_BUILD_LINE_SITES) {
			return;
		}
		std::vector<coord::phys3> line;
		line.reserve(sites.size());
		for (const auto &site : sites) {
			line.push_back(site.to_phys3_center());
		}
		network::CommandIntent intent{
			0, network::CommandKind::BUILD_TOWER, selection.ids, line.front()};
		intent.queue = true;
		intent.build_line = std::move(line);
		this->commands->dispatch(intent, player_of(*controller));
		return;
	}
	if (not release.click) {
		throw Error{ERR << "Shift-click target was not captured"};
	}
	const auto current_selection = controller->get_selection_details();
	if (not controller->is_tower_placement_active()
	    or controller->get_controlled() != release.click->owner
	    or current_selection.ids != selection.ids
	    or current_selection.kind != SelectionKind::BUILDER) {
		return;
	}
	this->place_tower(release.mouse_pos, true, controller);
}

void GameBindings::select_guest(const Release &release,
                                const controller_t &controller,
                                const SelectionDetails &selection,
                                uint64_t generation,
                                std::shared_ptr<const network::WorldState> replica) const {
	if (not release.dragged()) {
		this->pick_at_click(release, [controller, commands = this->commands, camera = this->camera,
		                              selection, shift = release.shift(), generation, replica](
										 std::optional<gamestate::entity_id_t> id) {
			if (id and apply_structure_click(controller, *commands, camera, *id, generation, selection, shift, false)) {
				return;
			}
			apply_guest_selection(controller, *replica, generation,
			                      id ? std::vector<gamestate::entity_id_t>{*id}
			                         : std::vector<gamestate::entity_id_t>{});
		});
		return;
	}

	// Drag-select locally owned Horses or a Builder from authoritative
	// positions, without querying the paused guest simulation.
	if (not release.projection) {
		throw Error{ERR << "Drag selection projection was not captured"};
	}
	const auto &projection = *release.projection;
	const auto &start = *release.start;
	const auto &end = release.args.mouse;
	std::vector<gamestate::entity_id_t> picked;
	for (const auto &entity : replica->entities) {
		if (entity.owner_id != replica->local_player_id
		    or not network::is_unit(entity.kind())) {
			continue;
		}
		auto scene = entity.position.to_scene3().to_world_space();
		Eigen::Vector4f clip = projection.matrix * Eigen::Vector4f{scene.x(), scene.y(), scene.z(), 1.0f};
		if (clip.w() <= 0) {
			continue;
		}
		double x = (clip.x() / clip.w() * 0.5 + 0.5) * projection.viewport[0];
		double y = (0.5 - clip.y() / clip.w() * 0.5) * projection.viewport[1];
		if (x >= std::min(start.x, end.x) and x <= std::max(start.x, end.x)
		    and y >= std::min(start.y, end.y) and y <= std::max(start.y, end.y)) {
			picked.push_back(entity.entity_id);
			// Builder controls currently accept exactly one unit.
			if (entity.kind() == network::EntityKind::BUILDER) {
				break;
			}
		}
	}
	apply_guest_selection(controller, *replica, generation, picked);
}

void GameBindings::select_local(const Release &release,
                                const controller_t &controller,
                                const SelectionDetails &selection,
                                uint64_t generation) const {
	if (not release.dragged()) {
		this->pick_at_click(release, [simulation = this->simulation, time_loop = this->time_loop,
		                              camera = this->camera, commands = this->commands, controller,
		                              selection, shift = release.shift(),
		                              controlled = controller->get_controlled(), generation](
										 std::optional<gamestate::entity_id_t> id) {
			if (id and apply_structure_click(controller, *commands, camera, *id, generation, selection, shift, false)) {
				return;
			}
			std::vector<gamestate::entity_id_t> picked;
			if (id.has_value()) {
				picked.push_back(*id);
			}
			event::EventHandler::param_map::map_t params{
				{"controlled", controlled},
				{"entity_ids", picked},
				{"select_cb",
			     std::function<void(const std::vector<gamestate::entity_id_t>)>{
					 [controller, simulation, generation](const std::vector<gamestate::entity_id_t> ids) {
						 apply_selection(controller, simulation->get_game()->get_state(), generation, ids);
					 }}},
			};
			simulation->get_event_loop()->submit_event(
				"game.click_select",
				simulation->get_commander(),
				simulation->get_game()->get_state(),
				time_loop->get_clock()->get_time(),
				params);
		});
		return;
	}

	if (not release.projection) {
		throw Error{ERR << "Drag selection projection was not captured"};
	}
	event::EventHandler::param_map::map_t params{
		{"controlled", controller->get_controlled()},
		{"drag_start", release.projection->start},
		{"drag_end", release.projection->end},
		{"camera_matrix", release.projection->matrix},
		{"select_cb",
	     std::function<void(const std::vector<gamestate::entity_id_t> ids)>{
			 [controller, simulation = this->simulation, generation](
				 const std::vector<gamestate::entity_id_t> ids) {
				 apply_selection(controller, simulation->get_game()->get_state(), generation, ids);
			 }}},
	};
	this->simulation->get_event_loop()->submit_event(
		"game.drag_select",
		this->simulation->get_commander(),
		this->simulation->get_game()->get_state(),
		this->time_loop->get_clock()->get_time(),
		params);
}

void GameBindings::select_structure_group(const event_arguments &args,
                                          const controller_t &controller) const {
	controller->mark_structure_double_click();
	controller->reset_drag_select();
	if (controller->is_garrison_mode_active()
	    or controller->is_tower_placement_active()
	    or controller->is_attack_move_active()
	    or controller->is_patrol_active()) {
		return;
	}
	const auto previous = controller->get_selection_details();
	const auto generation = controller->begin_selection_update();
	this->pick(args.mouse, [camera = this->camera, controller, commands = this->commands, previous, generation](
							   std::optional<gamestate::entity_id_t> id) {
		if (id and apply_structure_click(controller, *commands, camera, *id, generation, previous, false, true)) {
			return;
		}
		// Ignore double-click on non-structures without losing the
		// selection created by the preceding single click.
		controller->set_selected_if_current(
			generation, previous.ids, previous.kind, previous.render_entity);
	});
}


void setup_defaults(const std::shared_ptr<BindingContext> &ctx,
                    const std::shared_ptr<Controller> &controller,
                    const std::shared_ptr<time::TimeLoop> &time_loop,
                    const std::shared_ptr<openage::gamestate::GameSimulation> &simulation,
                    const std::shared_ptr<renderer::camera::Camera> &camera,
                    const entity_picker_t &pick_entity,
                    const std::function<bool(network::CommandIntent)> &send_guest_command,
                    const std::function<std::optional<network::WorldState>()> &guest_world,
                    bool online_lobby,
                    const std::function<bool()> &can_spawn_hunter) {
	auto bindings = std::make_shared<GameBindings>(
		time_loop, simulation, camera, pick_entity,
		CommandDispatcher::for_simulation(simulation, time_loop, send_guest_command, guest_world),
		can_spawn_hunter);
	bindings->bind(ctx, controller, online_lobby);
}

} // namespace openage::input::game
