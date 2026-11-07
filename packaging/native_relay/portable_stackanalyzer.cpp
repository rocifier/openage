// Copyright 2026 the openage authors. See copying.md for legal info.
//
// The Pelican upload is fully static musl and has no optional system
// stack-symbolication libraries. This is diagnostic-only functionality.
// Keep the normal platform StackAnalyzer for all standard developer builds.
#include "error/stackanalyzer.h"

namespace openage::error {

void StackAnalyzer::analyze() {
    // Native relay exception messages remain available without backtraces.
}

void StackAnalyzer::get_symbols(
    std::function<void(const backtrace_symbol *)> /*callback*/,
    bool /*reversed*/) const {}

void StackAnalyzer::trim_to_current_stack_frame() {}

} // namespace openage::error
