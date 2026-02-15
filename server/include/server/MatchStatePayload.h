#pragma once

#include "server/MatchLifecycleSimulation.h"
#include "shared/net/Protocol.h"

#include <vector>

namespace devy::server {

nlohmann::json build_match_state_payload(const MatchLifecycleState& state,
                                         const std::vector<MatchScoreEntry>& scoreboard,
                                         bool include_scoreboard);

devy::net::Packet build_match_state_packet(const MatchLifecycleState& state,
                                           const std::vector<MatchScoreEntry>& scoreboard);

} // namespace devy::server
