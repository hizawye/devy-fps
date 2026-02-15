#include "server/CombatEvents.h"

namespace devy::server {

nlohmann::json to_snapshot_event(const DamageResolution& damage_event) {
  return {{"type", "damage_event"},
          {"tick", damage_event.tick},
          {"attacker_id", damage_event.attacker_id},
          {"victim_id", damage_event.victim_id},
          {"shot_seq", damage_event.shot_seq},
          {"weapon_id", damage_event.weapon_id},
          {"damage", damage_event.damage},
          {"victim_health", damage_event.victim_health},
          {"lethal", damage_event.lethal}};
}

nlohmann::json to_snapshot_event(const DeathResolution& death_event) {
  return {{"type", "death_event"},
          {"tick", death_event.tick},
          {"victim_id", death_event.victim_id},
          {"killer_id", death_event.killer_id},
          {"shot_seq", death_event.shot_seq},
          {"weapon_id", death_event.weapon_id}};
}

devy::net::Packet to_damage_packet(const DamageResolution& damage_event) {
  return {devy::net::MessageType::DamageEvent,
          {{"attacker_id", damage_event.attacker_id},
           {"victim_id", damage_event.victim_id},
           {"damage", damage_event.damage},
           {"lethal", damage_event.lethal},
           {"weapon_id", damage_event.weapon_id},
           {"shot_seq", damage_event.shot_seq},
           {"victim_health", damage_event.victim_health},
           {"tick", damage_event.tick}},
          devy::net::kProtocolVersion};
}

void append_combat_outputs(const CombatTickResult& tick_result, nlohmann::json& snapshot_events,
                           std::vector<devy::net::Packet>* reliable_broadcasts) {
  if (!snapshot_events.is_array()) {
    snapshot_events = nlohmann::json::array();
  }

  for (const auto& damage_event : tick_result.damage_events) {
    snapshot_events.push_back(to_snapshot_event(damage_event));
    if (reliable_broadcasts) {
      reliable_broadcasts->push_back(to_damage_packet(damage_event));
    }
  }

  for (const auto& death_event : tick_result.death_events) {
    snapshot_events.push_back(to_snapshot_event(death_event));
  }
}

} // namespace devy::server
