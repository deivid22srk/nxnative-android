// Cross-check an inspected NSP against a prod.keys set and render the result
// as JSON for the UI. Key values never enter the JSON.
#pragma once

#include <string>

#include "keys.h"
#include "nsp.h"

namespace nx2a {

std::string analyzeImport(const NspInfo& info, const KeySet& keys);

}  // namespace nx2a
