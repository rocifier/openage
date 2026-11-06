// Copyright 2013-2020 the openage authors. See copying.md for legal info.

#include "cvar.h"

#include "../log/log.h"

#include <functional>
#include <sstream>
#include <unordered_set>


namespace openage {
namespace cvar {

CVarManager::CVarManager(const util::Path &path, bool native_config) :
	path{path},
	native_config{native_config} {}


bool CVarManager::create(const std::string &name,
                         const std::pair<get_func, set_func> &accessors) {
	auto it = this->store.find(name);
	if (it == this->store.end()) {
		this->store[name] = accessors;
		return true;
	}
	return false;
}


std::string CVarManager::get(const std::string &name) const {
	auto it = this->store.find(name);
	if (it != this->store.end()) {
		return it->second.first();
	}
	return "";
}


void CVarManager::set(const std::string &name, const std::string &value) const {
	auto it = store.find(name);
	if (it != store.end()) {
		it->second.second(value);
	}
}


void CVarManager::load_config(const util::Path &path) {
	if (not this->native_config) {
		pyx_load_config_file.call(this, path);
		return;
	}

	// Use the same set/load syntax as openage/cvar/config_file.py, without
	// requiring Cython to register a callback before gameplay can start.
	std::unordered_set<std::string> loaded{};
	std::function<void(const util::Path &)> load = [&](const util::Path &file) {
		if (not file.is_file()) {
			log::log(INFO << "config file " << file << " not found.");
			return;
		}
		if (not loaded.insert(file.resolve_native_path_r()).second) {
			return;
		}
		log::log(INFO << "loading config file " << file << "...");
		for (const auto &line : file.open_r().get_lines()) {
			std::istringstream words{line};
			std::string action;
			words >> action;
			if (action == "set") {
				std::string key;
				std::string value;
				if (words >> key >> value) {
					std::string word;
					while (words >> word) {
						value += " " + word;
					}
					this->set(key, value);
				}
			}
			else if (action == "load") {
				std::string name;
				while (words >> name) {
					load(file.get_parent() / name);
				}
			}
		}
	};
	load(path);
}


void CVarManager::load_all() {
	log::log(INFO << "loading configuration files...");
	this->load_config(this->path["keybinds.oac"]);
	this->load_config(this->path["audio.oac"]);
}

pyinterface::PyIfFunc<void, CVarManager *, const util::Path &> pyx_load_config_file;

}} // openage::cvar
