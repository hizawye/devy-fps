#include "server/MatchStatePayload.h"

namespace devy::server {
namespace {

nlohmann::json scoreboard_to_json(const std::vector<MatchScoreEntry>& scoreboard) {
  nlohmann::json out = nlohmann::json::array();
  out.get_ref<nlohmann::json::array_t&>().reserve(scoreboard.size());
  for (const auto& score : scoreboard) {
    out.push_back({{"player_id", score.player_id},
                   {"kills", score.kills},
                   {"deaths", score.deaths},
                   {"coins", score.coins},
                   {"respawns_remaining", score.respawns_remaining},
                   {"alive", score.alive},
                   {"eliminated", score.eliminated},
                   {"connected", score.connected}});
  }
  return out;
}

} // namespace

nlohmann::json build_match_state_payload(const MatchLifecycleState& state,
                                         const std::vector<MatchScoreEntry>& scoreboard,
                                         bool include_scoreboard) {
  nlohmann::json payload = {{"state", devy::server::to_string(state.phase)},
                            {"remaining_seconds", state.remaining_seconds},
                            {"phase_started_tick", state.phase_started_tick}};
  if (include_scoreboard) {
    payload["scoreboard"] = scoreboard_to_json(scoreboard);
  }
  if (state.winner_player_id.has_value()) {
    payload["winner_player_id"] = state.winner_player_id.value();
  }
  return payload;
}

devy::net::Packet build_match_state_packet(const MatchLifecycleState& state,
                                           const std::vector<MatchScoreEntry>& scoreboard) {
  return {devy::net::MessageType::MatchState, build_match_state_payload(state, scoreboard, true),
          devy::net::kProtocolVersion};
}

} // namespace devy::server
