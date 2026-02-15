#pragma once

#include "shared/voxel/World.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace devy::client {

struct ChunkRevision {
  devy::voxel::ChunkCoord coord{};
  uint32_t revision{0U};
};

struct ChunkSyncDelta {
  std::vector<ChunkRevision> added{};
  std::vector<ChunkRevision> removed{};
  std::vector<ChunkRevision> deltas{};
};

struct ChunkSyncApplyResult {
  bool changed{false};
  std::size_t added_or_updated{0U};
  std::size_t removed{0U};
};

std::optional<ChunkSyncDelta> parse_chunk_sync(const nlohmann::json& snapshot_payload);

ChunkSyncApplyResult apply_chunk_sync(
    devy::voxel::World* world, int world_height, const ChunkSyncDelta& delta,
    std::unordered_map<devy::voxel::ChunkCoord, uint32_t, devy::voxel::ChunkCoordHash>* revisions);

} // namespace devy::client
