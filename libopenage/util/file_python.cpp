// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Developer-only adapter for Cython-owned Python file objects.

#include "file.h"

#include <memory>

#include "filelike/python.h"

namespace openage::util {

File::File(const py::Obj &filelike) {
	this->filelike = std::make_shared<filelike::Python>(filelike);
}

} // namespace openage::util
