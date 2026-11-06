// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Developer-only Python filesystem adapter. Not part of the native FVD
// client when FVD_WITH_PYTHON_ADAPTERS is OFF.

#include "path.h"

#include <memory>

#include "fslike/directory.h"
#include "fslike/python.h"

namespace openage::util {

Path::Path(const py::Obj &fsobj_in,
           const std::vector<std::string> &parts) {
	for (const auto &part : parts) {
		if (part.empty() or part == ".") continue;
		if (part == "..") {
			if (not this->parts.empty()) this->parts.pop_back();
		}
		else this->parts.push_back(part);
	}

	// Preserve the Cython filesystem compatibility optimization.
	if (fslike::pyx_fs_is_fslike_directory.call(fsobj_in.get_ref())) {
		this->fsobj = std::make_shared<fslike::Directory>(
			fsobj_in.getattr("path").bytes());
	}
	else {
		this->fsobj = std::make_shared<fslike::Python>(fsobj_in);
	}
}

} // namespace openage::util
