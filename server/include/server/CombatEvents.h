#pragma once

#include "server/CombatSimulation.h"
#include "shared/net/Protocol.h"

#include <nlohmann/json.hpp>

#include <vector>

namespace devy::server {

nlohmann::json to_snapshot_event(const DamageResolution& damage_event);
nlohmann::json to_snapshot_event(const DeathResolution& death_event);
devy::net::Packet to_damage_packet(const DamageResolution& damage_event);

void append_combat_outputs(const CombatTickResult& tick_result, nlohmann::json& snapshot_events,
                           std::vector<devy::net::Packet>* reliable_broadcasts);

} // namespace devy::server
