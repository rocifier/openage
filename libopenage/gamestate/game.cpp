// Copyright 2018-2024 the openage authors. See copying.md for legal info.

#include "game.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nyan/nyan.h>

#include "error/error.h"
#include "log/log.h"
#include "log/message.h"

#include "assets/mod_manager.h"
#include "assets/modpack.h"
#include "gamestate/entity_factory.h"
#include "gamestate/fvd.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_factory.h"
#include "gamestate/universe.h"
#include "util/path.h"
#include "util/strings.h"

#include "coord/tile.h"

namespace openage::gamestate {

Game::Game(const std::shared_ptr<openage::event::EventLoop> &event_loop,
           const std::shared_ptr<assets::ModManager> &mod_manager,
           const std::shared_ptr<EntityFactory> &entity_factory,
           const std::shared_ptr<TerrainFactory> &terrain_factory) :
	db{nyan::Database::create()},
	state{std::make_shared<GameState>(this->db, event_loop)},
	universe{std::make_shared<Universe>(state)} {
	this->load_data(mod_manager);
	this->state->set_mod_manager(mod_manager);

	const auto &mods = mod_manager->get_load_order();
	if (std::find(mods.begin(), mods.end(), "fvd") != mods.end()) {
		std::shared_ptr<FVD> scenario;
		try {
			scenario = std::make_shared<FVD>(this->state);
		}
		catch (Error &) {
			throw Error{ERR << "Failed to read FVD scenario settings from "
			                << "fvd/data/scenario.nyan"};
		}
		catch (const std::exception &exc) {
			throw Error{ERR << "Failed to read FVD scenario settings from "
			                << "fvd/data/scenario.nyan: " << exc.what(),
			            true,
			            false};
		}
		catch (...) {
			throw Error{ERR << "Failed to read FVD scenario settings from "
			                << "fvd/data/scenario.nyan: unknown error",
			            true,
			            false};
		}
		this->state->set_fvd(scenario);
		scenario->initialise(this->state, event_loop, entity_factory, terrain_factory);
		return;
	}

	// TODO: Testing player creation
	auto player1 = entity_factory->add_player(event_loop, state, "");
	auto player2 = entity_factory->add_player(event_loop, state, "");
	state->add_player(player1);
	state->add_player(player2);

	// TODO: This lets the spawner event check which modpacks are loaded,
	//       so that it can decide which entities it can spawn.
	//       This can be removed when we spawn based on game logic rather than
	//       hardcoded entity types.
	this->state->set_mod_manager(mod_manager);

	this->generate_terrain(terrain_factory);
}

const std::shared_ptr<GameState> &Game::get_state() const {
	return this->state;
}

void Game::attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory) {
	this->universe->attach_renderer(render_factory);
	this->state->get_map()->get_terrain()->attach_renderer(render_factory);
}

void Game::load_data(const std::shared_ptr<assets::ModManager> &mod_manager) {
	const auto load_order = mod_manager->get_load_order();

	// Mod IDs are nyan import namespaces, not necessarily physical folder names.
	// In a source checkout, engine data lives in fvd/engine_api, while its
	// module name is still "engine". Build a native filesystem lookup that
	// keeps both identities separate and works with the installed package too.
	auto writable_roots = std::make_shared<std::unordered_map<std::string, std::string>>();
	for (const auto &mod_id : load_order) {
		const auto &info = mod_manager->get_modpack(mod_id)->get_info();
		writable_roots->emplace(mod_id, info.path.resolve_native_path());
	}
	const std::shared_ptr<const std::unordered_map<std::string, std::string>> mod_roots = writable_roots;

	for (const auto &mod_id : load_order) {
		auto mod = mod_manager->get_modpack(mod_id);
		auto info = mod->get_info();

		auto includes = info.includes;
		for (const auto &include : includes) {
			// handle wildcards
			auto parts = util::split(include, '/');
			auto last_part = parts.back();
			bool recursive = false;
			auto search = include;
			if (last_part == "**") {
				recursive = true;
				if (parts.size() == 1) {
					// include = "**"
					// start in root directory
					search = "";
				}
				else {
					// include = "path/to/somewhere/**"
					// remove the wildcard '**' and the slash '/'
					search = include.substr(0, include.size() - 3);
				}
			}

			this->load_path(info.path, mod_id, search, recursive, mod_roots);
		}
	}
}

void Game::load_path(const util::Path &mod_path,
                     const std::string &mod_id,
                     const std::string &search,
                     bool recursive,
                     const std::shared_ptr<const std::unordered_map<std::string, std::string>> &mod_roots) {
	auto search_path = mod_path / search;

	// nyan asks for logical import paths (e.g. engine/util/foo.nyan).
	// Resolve those against each modpack's actual native directory, rather
	// than guessing that the import namespace matches the on-disk folder.
	// Capture roots by shared ownership because nyan may retain a fetcher.
	auto fileload_func = [mod_roots](const std::string &filename) {
		log::log(INFO << "Loading .nyan file: " << filename);
		const auto separator = filename.find('/');
		if (separator == std::string::npos) {
			throw Error{ERR << "Nyan module path lacks a modpack name: " << filename};
		}
		const auto found = mod_roots->find(filename.substr(0, separator));
		if (found == mod_roots->end()) {
			throw Error{ERR << "Unknown nyan modpack in import path: " << filename};
		}
		return std::make_shared<nyan::File>(found->second + filename.substr(separator));
	};

	// loading a physical file into the logical nyan namespace
	if (search_path.is_file() and search_path.get_suffix() == ".nyan") {
		const auto loc = mod_id + "/" + search;
		try {
			this->db->load(loc, fileload_func);
		}
		catch (Error &) {
			throw;
		}
		catch (const std::exception &exc) {
			throw Error{ERR << "Failed to load nyan file '" << loc << "': " << exc.what(),
			            true,
			            false};
		}
		catch (...) {
			throw Error{ERR << "Failed to load nyan file '" << loc << "': unknown error",
			            true,
			            false};
		}
		return;
	}

	// directory loading
	if (search_path.is_dir()) {
		for (auto p : search_path.iterdir()) {
			if (p.is_dir() and not recursive) {
				continue;
			}
			auto new_search = search + "/" + p.get_name();
			this->load_path(mod_path, mod_id, new_search, recursive, mod_roots);
		}
	}
}

void Game::generate_terrain(const std::shared_ptr<TerrainFactory> &terrain_factory) {
	auto chunk0 = terrain_factory->add_chunk(this->state,
	                                         util::Vector2s{10, 10},
	                                         coord::tile_delta{0, 0});
	auto chunk1 = terrain_factory->add_chunk(this->state,
	                                         util::Vector2s{10, 10},
	                                         coord::tile_delta{10, 0});
	auto chunk2 = terrain_factory->add_chunk(this->state,
	                                         util::Vector2s{10, 10},
	                                         coord::tile_delta{0, 10});
	auto chunk3 = terrain_factory->add_chunk(this->state,
	                                         util::Vector2s{10, 10},
	                                         coord::tile_delta{10, 10});

	auto terrain = terrain_factory->add_terrain({20, 20}, {chunk0, chunk1, chunk2, chunk3});

	auto map = std::make_shared<Map>(this->state, terrain);
	this->state->set_map(map);
}

} // namespace openage::gamestate
