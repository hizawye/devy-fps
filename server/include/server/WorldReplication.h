#pragma once

#include "shared/voxel/World.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devy::server {

struct ChunkCoord {
  int x{0};
  int y{0};
  int z{0};

  [[nodiscard]] bool operator==(const ChunkCoord& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct ChunkCoordHash {
  [[nodiscard]] std::size_t operator()(const ChunkCoord& coord) const noexcept;
};

struct ReplicationConfig {
  int chunks_x{64};
  int chunks_y{8};
  int chunks_z{64};
  int interest_radius_chunks{2};
};

struct ChunkRevisionEntry {
  ChunkCoord coord{};
  uint32_t revision{0};
};

struct PlayerWorldUpdate {
  uint32_t player_id{0};
  std::vector<ChunkRevisionEntry> added{};
  std::vector<ChunkRevisionEntry> removed{};
  std::vector<ChunkRevisionEntry> deltas{};
};

class WorldReplication {
 public:
  explicit WorldReplication(ReplicationConfig config = {});

  void reset();
  void remove_player(uint32_t player_id);
  void mark_chunk_dirty(const ChunkCoord& coord);

  [[nodiscard]] PlayerWorldUpdate build_player_update(uint32_t player_id,
                                                      float world_x,
                                                      float world_z,
                                                      const devy::voxel::World& world);

 private:
  struct PlayerReplicationState {
    std::unordered_set<ChunkCoord, ChunkCoordHash> subscribed_chunks{};
    std::unordered_map<ChunkCoord, uint32_t, ChunkCoordHash> known_revisions{};
  };

  [[nodiscard]] static ReplicationConfig sanitize_config(ReplicationConfig config);
  [[nodiscard]] std::vector<ChunkCoord> gather_relevant_chunks(float world_x,
                                                               float world_z,
                                                               const devy::voxel::World& world) const;
  [[nodiscard]] uint32_t revision_for(const ChunkCoord& coord) const;

  ReplicationConfig config_{};
  std::unordered_map<uint32_t, PlayerReplicationState> player_states_{};
  std::unordered_map<ChunkCoord, uint32_t, ChunkCoordHash> chunk_revisions_{};
};

} // namespace devy::server
