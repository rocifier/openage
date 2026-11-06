// Copyright 2026 the openage authors. See copying.md for legal info.

#include "mounted_directories.h"

#include <ostream>
#include <utility>

#include "directory.h"
#include "error/error.h"

namespace openage::util::fslike {

MountedDirectories::MountedDirectories(const std::string &asset_directory,
                                       const std::string &config_directory) :
	mounts{
		{"assets", {std::make_shared<Directory>(asset_directory), false}},
		{"cfg", {std::make_shared<Directory>(config_directory), true}},
		{"fvd_placeholder_sounds", {std::make_shared<Directory>(config_directory + "/fvd_placeholder_sounds"), false}},
	} {}

const MountedDirectories::Mount &MountedDirectories::get_mount(const Path::parts_t &parts) const {
	if (parts.empty()) {
		throw Error{MSG(err) << "Native virtual root requires a mount name"};
	}
	auto found = this->mounts.find(parts.front());
	if (found == this->mounts.end()) {
		throw Error{MSG(err) << "Unknown native filesystem mount: " << parts.front()};
	}
	return found->second;
}

Path::parts_t MountedDirectories::relative_parts(const Path::parts_t &parts) const {
	return {parts.begin() + 1, parts.end()};
}

void MountedDirectories::require_writable(const Path::parts_t &parts) const {
	if (not this->get_mount(parts).writable) {
		throw Error{MSG(err) << "Packaged assets are read-only"};
	}
}

bool MountedDirectories::is_file(const Path::parts_t &parts) {
	if (parts.size() < 2 or not this->mounts.contains(parts.front())) {
		return false;
	}
	return this->get_mount(parts).directory->is_file(this->relative_parts(parts));
}

bool MountedDirectories::is_dir(const Path::parts_t &parts) {
	if (parts.empty()) {
		return true;
	}
	if (not this->mounts.contains(parts.front())) {
		return false;
	}
	return this->get_mount(parts).directory->is_dir(this->relative_parts(parts));
}

bool MountedDirectories::writable(const Path::parts_t &parts) {
	if (parts.empty() or not this->mounts.contains(parts.front())) {
		return false;
	}
	const auto &mount = this->get_mount(parts);
	return mount.writable and mount.directory->writable(this->relative_parts(parts));
}

Path::parts_t MountedDirectories::list(const Path::parts_t &parts) {
	if (parts.empty()) {
		return {"assets", "cfg", "fvd_placeholder_sounds"};
	}
	return this->get_mount(parts).directory->list(this->relative_parts(parts));
}

bool MountedDirectories::mkdirs(const Path::parts_t &parts) {
	if (parts.empty()) {
		return true;
	}
	this->require_writable(parts);
	return this->get_mount(parts).directory->mkdirs(this->relative_parts(parts));
}

File MountedDirectories::open_r(const Path::parts_t &parts) {
	return this->get_mount(parts).directory->open_r(this->relative_parts(parts));
}

File MountedDirectories::open_w(const Path::parts_t &parts) {
	this->require_writable(parts);
	return this->get_mount(parts).directory->open_w(this->relative_parts(parts));
}

File MountedDirectories::open_rw(const Path::parts_t &parts) {
	this->require_writable(parts);
	return this->get_mount(parts).directory->open_rw(this->relative_parts(parts));
}

File MountedDirectories::open_a(const Path::parts_t &parts) {
	this->require_writable(parts);
	return this->get_mount(parts).directory->open_a(this->relative_parts(parts));
}

File MountedDirectories::open_ar(const Path::parts_t &parts) {
	this->require_writable(parts);
	return this->get_mount(parts).directory->open_ar(this->relative_parts(parts));
}

std::string MountedDirectories::get_native_path(const Path::parts_t &parts) {
	return this->get_mount(parts).directory->get_native_path(this->relative_parts(parts));
}

bool MountedDirectories::rename(const Path::parts_t &parts, const Path::parts_t &target_parts) {
	this->require_writable(parts);
	this->require_writable(target_parts);
	if (parts.front() != target_parts.front()) {
		throw Error{MSG(err) << "Cannot rename across native mounts"};
	}
	return this->get_mount(parts).directory->rename(
		this->relative_parts(parts), this->relative_parts(target_parts));
}

bool MountedDirectories::rmdir(const Path::parts_t &parts) {
	this->require_writable(parts);
	if (parts.size() < 2) {
		throw Error{MSG(err) << "Cannot remove native mount root"};
	}
	return this->get_mount(parts).directory->rmdir(this->relative_parts(parts));
}

bool MountedDirectories::touch(const Path::parts_t &parts) {
	this->require_writable(parts);
	return this->get_mount(parts).directory->touch(this->relative_parts(parts));
}

bool MountedDirectories::unlink(const Path::parts_t &parts) {
	this->require_writable(parts);
	return this->get_mount(parts).directory->unlink(this->relative_parts(parts));
}

int MountedDirectories::get_mtime(const Path::parts_t &parts) {
	return this->get_mount(parts).directory->get_mtime(this->relative_parts(parts));
}

uint64_t MountedDirectories::get_filesize(const Path::parts_t &parts) {
	return this->get_mount(parts).directory->get_filesize(this->relative_parts(parts));
}

std::ostream &MountedDirectories::repr(std::ostream &stream) {
	return stream << "NativeMountedDirectories";
}

} // namespace openage::util::fslike
