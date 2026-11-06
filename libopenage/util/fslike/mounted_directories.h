// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <map>
#include <memory>
#include <string>

#include "../compiler.h"
#include "fslike.h"

namespace openage::util::fslike {

class Directory;

/**
 * Native-only virtual root for FVD's assets/ and cfg/ mounts.
 *
 * Keeping the mount table in C++ avoids importing Python's Union FSLike
 * during gameplay. Only cfg/ is writable.
 */
class OAAPI MountedDirectories final : public FSLike {
public:
	MountedDirectories(const std::string &asset_directory,
	                   const std::string &config_directory);

	bool is_file(const Path::parts_t &parts) override;
	bool is_dir(const Path::parts_t &parts) override;
	bool writable(const Path::parts_t &parts) override;
	Path::parts_t list(const Path::parts_t &parts) override;
	bool mkdirs(const Path::parts_t &parts) override;
	File open_r(const Path::parts_t &parts) override;
	File open_w(const Path::parts_t &parts) override;
	File open_rw(const Path::parts_t &parts) override;
	File open_a(const Path::parts_t &parts) override;
	File open_ar(const Path::parts_t &parts) override;
	std::string get_native_path(const Path::parts_t &parts) override;
	bool rename(const Path::parts_t &parts,
	            const Path::parts_t &target_parts) override;
	bool rmdir(const Path::parts_t &parts) override;
	bool touch(const Path::parts_t &parts) override;
	bool unlink(const Path::parts_t &parts) override;
	int get_mtime(const Path::parts_t &parts) override;
	uint64_t get_filesize(const Path::parts_t &parts) override;
	std::ostream &repr(std::ostream &stream) override;

private:
	struct Mount {
		std::shared_ptr<Directory> directory;
		bool writable;
	};
	const Mount &get_mount(const Path::parts_t &parts) const;
	Path::parts_t relative_parts(const Path::parts_t &parts) const;
	void require_writable(const Path::parts_t &parts) const;

	std::map<std::string, Mount> mounts;
};

} // namespace openage::util::fslike
