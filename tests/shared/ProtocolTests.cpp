#include "shared/net/Protocol.h"

#include <catch2/catch_test_macros.hpp>

namespace devy::net {
namespace {

TEST_CASE("Protocol packet round-trip preserves type and payload") {
  Packet in{MessageType::InventoryUpdate, {{"player_id", 4}, {"coins", 250}}, kProtocolVersion};

  const std::string encoded = serialize(in);
  const ParseResult parsed = try_deserialize(encoded);

  REQUIRE(parsed.ok());
  REQUIRE(parsed.packet.version == kProtocolVersion);
  REQUIRE(parsed.packet.type == MessageType::InventoryUpdate);
  REQUIRE(parsed.packet.payload["player_id"] == 4);
  REQUIRE(parsed.packet.payload["coins"] == 250);
}

TEST_CASE("Protocol deserialize returns invalid packet on malformed data") {
  const Packet out = deserialize("{ this is not valid json");
  REQUIRE(out.type == MessageType::Invalid);
  REQUIRE(out.payload.empty());
  REQUIRE(out.version == kProtocolVersion);
}

TEST_CASE("Protocol parser supports missing version for backward compatibility") {
  const ParseResult parsed = try_deserialize(R"({
    "type": 6,
    "payload": {
      "player_id": 11,
      "coins": 75
    }
  })");

  REQUIRE(parsed.ok());
  REQUIRE(parsed.packet.type == MessageType::InventoryUpdate);
  REQUIRE(parsed.packet.version == kProtocolVersion);
}

TEST_CASE("Protocol parser rejects unsupported protocol versions") {
  const ParseResult parsed = try_deserialize(R"({
    "version": 999,
    "type": 6,
    "payload": {
      "player_id": 11,
      "coins": 75
    }
  })");

  REQUIRE_FALSE(parsed.ok());
  REQUIRE(parsed.error == ProtocolError::UnsupportedVersion);
}

TEST_CASE("Protocol parser rejects malformed envelopes") {
  SECTION("type missing") {
    const ParseResult parsed = try_deserialize(R"({"version":1,"payload":{}})");
    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::MissingEnvelopeField);
  }

  SECTION("type is unknown") {
    const ParseResult parsed = try_deserialize(R"({"version":1,"type":255,"payload":{}})");
    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::UnknownMessageType);
  }

  SECTION("payload is not an object") {
    const ParseResult parsed = try_deserialize(R"({"version":1,"type":6,"payload":[]})");
    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::InvalidPayloadType);
  }
}

TEST_CASE("Protocol parser validates payload schema rules") {
  SECTION("missing required field is rejected") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 6,
      "payload": {
        "player_id": 9
      }
    })");

    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::MissingPayloadField);
  }

  SECTION("wrong field type is rejected") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 6,
      "payload": {
        "player_id": "nine",
        "coins": 30
      }
    })");

    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::InvalidPayloadFieldType);
  }

  SECTION("unknown extra fields are tolerated for forward compatibility") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 6,
      "payload": {
        "player_id": 7,
        "coins": 30,
        "experimental_bonus": 99
      }
    })");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.packet.payload["experimental_bonus"] == 99);
  }
}

TEST_CASE("Protocol parser validates heartbeat payload") {
  SECTION("valid heartbeat is accepted") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 9,
      "payload": {
        "player_id": 4,
        "client_time_ms": 1200
      }
    })");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.packet.type == MessageType::Heartbeat);
    REQUIRE(parsed.packet.payload["player_id"] == 4);
  }

  SECTION("missing player_id is rejected") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 9,
      "payload": {
        "client_time_ms": 1200
      }
    })");

    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::MissingPayloadField);
  }
}

TEST_CASE("Protocol parser validates player_input movement-intent payload contract") {
  SECTION("valid player_input payload with sprint/crouch is accepted") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 3,
      "payload": {
        "player_id": 4,
        "input_seq": 12,
        "move_x": 1.0,
        "move_y": 0.0,
        "jump": false,
        "sprint": true,
        "crouch": false,
        "fire": false
      }
    })");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.packet.type == MessageType::PlayerInput);
    REQUIRE(parsed.packet.payload["sprint"] == true);
  }

  SECTION("missing required sprint/crouch fields is rejected") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 3,
      "payload": {
        "player_id": 4,
        "input_seq": 12,
        "move_x": 1.0,
        "move_y": 0.0,
        "jump": false,
        "fire": false
      }
    })");

    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::MissingPayloadField);
  }
}

TEST_CASE("Protocol parser accepts state snapshots with reconciliation scaffolding fields") {
  const ParseResult parsed = try_deserialize(R"({
    "version": 1,
    "type": 4,
    "payload": {
      "tick": 42,
      "players": [
        {
          "player_id": 1,
          "player_name": "alpha",
          "position": { "x": 10.5, "y": -2.0 },
          "velocity": { "x": 0.0, "y": 6.0 },
          "last_processed_input_seq": 99
        }
      ],
      "events": [],
      "chunk_sync": {
        "added": [{ "x": 0, "y": 0, "z": 0, "revision": 0 }],
        "removed": [],
        "deltas": []
      }
    }
  })");

  REQUIRE(parsed.ok());
  REQUIRE(parsed.packet.type == MessageType::StateSnapshot);
  REQUIRE(parsed.packet.payload["tick"] == 42);
  REQUIRE(parsed.packet.payload["players"].is_array());
  REQUIRE(parsed.packet.payload["players"][0]["last_processed_input_seq"] == 99);
  REQUIRE(parsed.packet.payload["chunk_sync"]["added"].is_array());
}

TEST_CASE("Protocol parser accepts block updates with optional player_id") {
  const ParseResult parsed = try_deserialize(R"({
    "version": 1,
    "type": 5,
    "payload": {
      "player_id": 7,
      "x": 12,
      "y": 8,
      "z": 3,
      "block_id": 0
    }
  })");

  REQUIRE(parsed.ok());
  REQUIRE(parsed.packet.type == MessageType::BlockUpdate);
  REQUIRE(parsed.packet.payload["player_id"] == 7);
}

TEST_CASE("Protocol parser validates weapon_fire payload contract") {
  SECTION("valid weapon_fire payload is accepted") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 10,
      "payload": {
        "player_id": 3,
        "shot_seq": 15,
        "weapon_id": "rifle",
        "origin": { "x": 10.0, "y": -4.0 },
        "direction": { "x": 1.0, "y": 0.0 }
      }
    })");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.packet.type == MessageType::WeaponFire);
    REQUIRE(parsed.packet.payload["shot_seq"] == 15);
  }

  SECTION("missing required weapon_fire field is rejected") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 10,
      "payload": {
        "player_id": 3,
        "weapon_id": "rifle",
        "origin": { "x": 10.0, "y": -4.0 },
        "direction": { "x": 1.0, "y": 0.0 }
      }
    })");

    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::MissingPayloadField);
  }
}

TEST_CASE("Protocol parser validates match_state payload contract") {
  SECTION("valid match_state payload is accepted") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 8,
      "payload": {
        "state": "in_match",
        "remaining_seconds": 45,
        "scoreboard": [
          {
            "player_id": 1,
            "kills": 3,
            "deaths": 1
          }
        ]
      }
    })");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.packet.type == MessageType::MatchState);
    REQUIRE(parsed.packet.payload["state"] == "in_match");
    REQUIRE(parsed.packet.payload["remaining_seconds"] == 45);
  }

  SECTION("missing required match_state field is rejected") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 8,
      "payload": {
        "state": "post_match"
      }
    })");

    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::MissingPayloadField);
  }
}

TEST_CASE("Protocol parser validates treasure_pickup payload contract") {
  SECTION("valid treasure_pickup payload is accepted") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 11,
      "payload": {
        "player_id": 4,
        "pickup_seq": 9,
        "spawn_id": 1001
      }
    })");

    REQUIRE(parsed.ok());
    REQUIRE(parsed.packet.type == MessageType::TreasurePickup);
    REQUIRE(parsed.packet.payload["pickup_seq"] == 9);
    REQUIRE(parsed.packet.payload["spawn_id"] == 1001);
  }

  SECTION("missing required treasure_pickup field is rejected") {
    const ParseResult parsed = try_deserialize(R"({
      "version": 1,
      "type": 11,
      "payload": {
        "player_id": 4,
        "spawn_id": 1001
      }
    })");

    REQUIRE_FALSE(parsed.ok());
    REQUIRE(parsed.error == ProtocolError::MissingPayloadField);
  }
}

} // namespace
} // namespace devy::net
