// Copyright 2016-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <functional>
#include <stdlib.h>
// pxd: from libcpp.string cimport string
#include <string>
#include <unordered_map>
#include <utility>

// pxd: from libopenage.util.path cimport Path
#include "../util/path.h"


namespace openage {
namespace cvar {

/** function type used to get a configuration value */
using get_func = std::function<std::string()>;

/** function type to set the value of a config entry */
using set_func = std::function<void(std::string)>;


/**
 * Configuration manager.
 *
 * Stores key-value pairs of config data.
 * Actually it doesn't store data, instead functions
 * that perform/fetch the configuration at the appropriate place.
 *
 * pxd:
 *
 * cppclass CVarManager:
 *     string get(string name) except +
 *     void set(string name, string value) except +
 *     void load_config(Path path) except +
 */
class OAAPI CVarManager {
public:
	CVarManager(const util::Path &path, bool native_config = false);

	/**
	 * Creates a configuration entry
	 * @returns if the entry name was successful.
	 *          It won't be if the name was used before.
	 */
	bool create(const std::string &name,
	            const std::pair<get_func, set_func> &accessors);

	/**
	 * Gets the value of a config entry.
	 * Internally calls the stored get function.
	 */
	std::string get(const std::string &name) const;

	/**
	 * Sets the config entry value.
	 */
	void set(const std::string &name, const std::string &value) const;

	/**
	 * Performs the loading of a configuration file
	 * using the native FVD configuration parser.
	 */
	void load_config(const util::Path &path);

	/**
	 * Perform the load of the default config files.
	 */
	void load_all();

private:
	/**
	 * Store the key-value pair of config options.
	 * The clue is to store the "what does the config do",
	 * not the actual value.
	 * That way the system is universal.
	 */
	std::unordered_map<std::string, std::pair<get_func, set_func>> store;

	/**
	 * Magic path that stores config files.
	 * The native startup supplies a writable application configuration path.
	 */
	util::Path path;

};


} // namespace cvar
} // namespace openage
