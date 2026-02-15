#include "server/WorldReplication.h"

#include <algorithm>
#include <cmath>

namespace devy::server {
namespace {

constexpr int kDefaultChunksX = 64;
constexpr int kDefaultChunksY = 8;
constexpr int kDefaultChunksZ = 64;
constexpr int kDefaultInterestRadiusChunks = 2;

int floor_div_chunk(float world_axis) {
  if (!std::isfinite(world_axis)) {
    return 0;
  }
  const float chunk = world_axis / static_cast<float>(devy::voxel::kChunkSize);
  return static_cast<int>(std::floor(chunk));
}

bool chunk_coord_less(const ChunkCoord& lhs, const ChunkCoord& rhs) {
  if (lhs.x != rhs.x) {
    return lhs.x < rhs.x;
  }
  if (lhs.z != rhs.z) {
    return lhs.z < rhs.z;
  }
  return lhs.y < rhs.y;
}

bool chunk_entry_less(const ChunkRevisionEntry& lhs, const ChunkRevisionEntry& rhs) {
  return chunk_coord_less(lhs.coord, rhs.coord);
}

} // namespace

std::size_t ChunkCoordHash::operator()(const ChunkCoord& coord) const noexcept {
  std::size_t hx = static_cast<std::size_t>(coord.x) * 73856093u;
  std::size_t hy = static_cast<std::size_t>(coord.y) * 19349663u;
  std::size_t hz = static_cast<std::size_t>(coord.z) * 83492791u;
  return hx ^ hy ^ hz;
}

WorldReplication::WorldReplication(ReplicationConfig config) : config_(sanitize_config(config)) {}

void WorldReplication::reset() {
  player_states_.clear();
  chunk_revisions_.clear();
}

void WorldReplication::remove_player(uint32_t player_id) { player_states_.erase(player_id); }

void WorldReplication::mark_chunk_dirty(const ChunkCoord& coord) {
  const auto it = chunk_revisions_.find(coord);
  if (it == chunk_revisions_.end()) {
    chunk_revisions_[coord] = 1U;
    return;
  }
  ++it->second;
}

PlayerWorldUpdate WorldReplication::build_player_update(uint32_t player_id,
                                                        float world_x,
                                                        float world_z,
                                                        const devy::voxel::World& world) {
  PlayerWorldUpdate update{};
  update.player_id = player_id;

  auto& player_state = player_states_[player_id];
  auto target_chunks = gather_relevant_chunks(world_x, world_z, world);
  std::unordered_set<ChunkCoord, ChunkCoordHash> target_set(target_chunks.begin(), target_chunks.end());

  std::vector<ChunkCoord> removed_coords{};
  removed_coords.reserve(player_state.subscribed_chunks.size());
  for (const auto& coord : player_state.subscribed_chunks) {
    if (target_set.find(coord) == target_set.end()) {
      removed_coords.push_back(coord);
    }
  }

  for (const auto& coord : removed_coords) {
    player_state.subscribed_chunks.erase(coord);
    player_state.known_revisions.erase(coord);
    update.removed.push_back({coord, 0U});
  }

  for (const auto& coord : target_chunks) {
    const uint32_t revision = revision_for(coord);
    const auto [subscribed_it, inserted] = player_state.subscribed_chunks.insert(coord);
    if (inserted) {
      static_cast<void>(subscribed_it);
      player_state.known_revisions[coord] = revision;
      update.added.push_back({coord, revision});
      continue;
    }

    auto known_it = player_state.known_revisions.find(coord);
    const uint32_t known_revision =
        (known_it == player_state.known_revisions.end()) ? 0U : known_it->second;
    if (revision > known_revision) {
      player_state.known_revisions[coord] = revision;
      update.deltas.push_back({coord, revision});
    }
  }

  std::sort(update.added.begin(), update.added.end(), chunk_entry_less);
  std::sort(update.removed.begin(), update.removed.end(), chunk_entry_less);
  std::sort(update.deltas.begin(), update.deltas.end(), chunk_entry_less);
  return update;
}

ReplicationConfig WorldReplication::sanitize_config(ReplicationConfig config) {
  if (config.chunks_x <= 0) {
    config.chunks_x = kDefaultChunksX;
  }
  if (config.chunks_y <= 0) {
    config.chunks_y = kDefaultChunksY;
  }
  if (config.chunks_z <= 0) {
    config.chunks_z = kDefaultChunksZ;
  }
  if (config.interest_radius_chunks < 0) {
    config.interest_radius_chunks = kDefaultInterestRadiusChunks;
  }
  return config;
}

std::vector<ChunkCoord> WorldReplication::gather_relevant_chunks(float world_x,
                                                                  float world_z,
                                                                  const devy::voxel::World& world) const {
  std::vector<ChunkCoord> coords{};
  coords.reserve(static_cast<std::size_t>(config_.chunks_y));

  if (config_.chunks_x <= 0 || config_.chunks_y <= 0 || config_.chunks_z <= 0) {
    return coords;
  }

  const int center_chunk_x =
      std::clamp(floor_div_chunk(world_x), 0, std::max(0, config_.chunks_x - 1));
  const int center_chunk_z =
      std::clamp(floor_div_chunk(world_z), 0, std::max(0, config_.chunks_z - 1));

  const int radius = config_.interest_radius_chunks;
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const int distance_sq = (dx * dx) + (dz * dz);
      if (distance_sq > (radius * radius)) {
        continue;
      }

      const int cx = center_chunk_x + dx;
      const int cz = center_chunk_z + dz;
      if (cx < 0 || cx >= config_.chunks_x || cz < 0 || cz >= config_.chunks_z) {
        continue;
      }

      for (int cy = 0; cy < config_.chunks_y; ++cy) {
        if (!world.get_chunk(cx, cy, cz)) {
          continue;
        }
        coords.push_back({cx, cy, cz});
      }
    }
  }

  std::sort(coords.begin(), coords.end(), chunk_coord_less);
  return coords;
}

uint32_t WorldReplication::revision_for(const ChunkCoord& coord) const {
  const auto it = chunk_revisions_.find(coord);
  if (it == chunk_revisions_.end()) {
    return 0U;
  }
  return it->second;
}

} // namespace devy::server
