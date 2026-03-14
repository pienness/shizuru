#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "policy/types.h"

namespace shizuru::core {

struct PolicyConfig {
  std::vector<PolicyRule> initial_rules;                 // Rules loaded at session start
  std::unordered_set<std::string> default_capabilities;  // Capabilities granted by default
};

}  // namespace shizuru::core
