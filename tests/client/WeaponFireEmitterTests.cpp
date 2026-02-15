#include "client/WeaponFireEmitter.h"
#include "shared/net/Protocol.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace devy::client {
namespace {

TEST_CASE("Weapon fire emitter emits protocol-compliant packets with normalized direction") {
  WeaponFireEmitter emitter;

  const auto first =
      emitter.emit(7U, "rifle", 10.0F, -2.0F, 3.0F, 4.0F, 1234U);
  REQUIRE(first.accepted());
  REQUIRE(first.shot_seq == 1U);
  REQUIRE(first.packet.type == devy::net::MessageType::WeaponFire);
  REQUIRE(first.packet.version == devy::net::kProtocolVersion);
  REQUIRE(first.packet.payload["player_id"] == 7U);
  REQUIRE(first.packet.payload["shot_seq"] == 1U);
  REQUIRE(first.packet.payload["weapon_id"] == "rifle");
  REQUIRE(first.packet.payload["client_fire_time_ms"] == 1234U);
  REQUIRE(first.packet.payload["direction"]["x"].get<float>() ==
          Catch::Approx(0.6F).margin(0.0001F));
  REQUIRE(first.packet.payload["direction"]["y"].get<float>() ==
          Catch::Approx(0.8F).margin(0.0001F));

  const auto second = emitter.emit(7U, "rifle", 10.0F, -2.0F, 1.0F, 0.0F);
  REQUIRE(second.accepted());
  REQUIRE(second.shot_seq == 2U);
}

TEST_CASE("Weapon fire emitter rejects invalid fire request inputs") {
  WeaponFireEmitter emitter;

  SECTION("player id must be non-zero") {
    const auto result = emitter.emit(0U, "rifle", 0.0F, 0.0F, 1.0F, 0.0F);
    REQUIRE_FALSE(result.accepted());
    REQUIRE(result.status == WeaponFireEmitStatus::InvalidPlayerId);
    REQUIRE(emitter.next_shot_seq() == 1U);
  }

  SECTION("weapon id must be non-empty") {
    const auto result = emitter.emit(3U, "", 0.0F, 0.0F, 1.0F, 0.0F);
    REQUIRE_FALSE(result.accepted());
    REQUIRE(result.status == WeaponFireEmitStatus::InvalidWeaponId);
    REQUIRE(emitter.next_shot_seq() == 1U);
  }

  SECTION("direction must have non-zero magnitude") {
    const auto result = emitter.emit(3U, "rifle", 0.0F, 0.0F, 0.0F, 0.0F);
    REQUIRE_FALSE(result.accepted());
    REQUIRE(result.status == WeaponFireEmitStatus::InvalidDirection);
    REQUIRE(emitter.next_shot_seq() == 1U);
  }
}

} // namespace
} // namespace devy::client
