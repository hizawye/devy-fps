#include "client/ChunkSyncApplier.h"

#include <cstdint>
#include <limits>

namespace devy::client {
namespace {

std::optional<uint32_t> json_to_u32(const nlohmann::json& value) {
  if (value.is_number_unsigned()) {
    const uint64_t raw = value.get<uint64_t>();
    if (raw <= static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
      return static_cast<uint32_t>(raw);
    }
    return std::nullopt;
  }
  if (value.is_number_integer()) {
    const int64_t raw = value.get<int64_t>();
    if (raw >= 0 && raw <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      return static_cast<uint32_t>(raw);
    }
  }
  return std::nullopt;
}

std::optional<int32_t> json_to_i32(const nlohmann::json& value) {
  if (value.is_number_integer()) {
    const int64_t raw = value.get<int64_t>();
    if (raw < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
        raw > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
      return std::nullopt;
    }
    return static_cast<int32_t>(raw);
  }
  if (value.is_number_unsigned()) {
    const uint64_t raw = value.get<uint64_t>();
    if (raw > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      return std::nullopt;
    }
    return static_cast<int32_t>(raw);
  }
  return std::nullopt;
}

std::optional<ChunkRevision> parse_revision(const nlohmann::json& value) {
  if (!value.is_object()) {
    return std::nullopt;
  }
  const auto x = json_to_i32(value.value("x", nlohmann::json{}));
  const auto y = json_to_i32(value.value("y", nlohmann::json{}));
  const auto z = json_to_i32(value.value("z", nlohmann::json{}));
  const auto revision = json_to_u32(value.value("revision", nlohmann::json{}));
  if (!x.has_value() || !y.has_value() || !z.has_value() || !revision.has_value()) {
    return std::nullopt;
  }
  if (x.value() < 0 || y.value() < 0 || z.value() < 0) {
    return std::nullopt;
  }
  return ChunkRevision{{x.value(), y.value(), z.value()}, revision.value()};
}

void parse_array(const nlohmann::json& root, const char* field, std::vector<ChunkRevision>* out) {
  if (out == nullptr || !root.contains(field) || !root[field].is_array()) {
    return;
  }
  for (const auto& value : root[field]) {
    const auto parsed = parse_revision(value);
    if (parsed.has_value()) {
      out->push_back(parsed.value());
    }
  }
}

void apply_added_like(
    devy::voxel::World* world, int world_height, const std::vector<ChunkRevision>& revisions,
    std::unordered_map<devy::voxel::ChunkCoord, uint32_t, devy::voxel::ChunkCoordHash>* known,
    ChunkSyncApplyResult* result) {
  if (world == nullptr || known == nullptr || result == nullptr) {
    return;
  }
  for (const auto& entry : revisions) {
    const auto existing = known->find(entry.coord);
    if (existing != known->end() && existing->second == entry.revision) {
      continue;
    }
    static_cast<void>(world->ensure_generated_chunk(entry.coord.x, entry.coord.y, entry.coord.z,
                                                     world_height));
    (*known)[entry.coord] = entry.revision;
    result->changed = true;
    result->added_or_updated += 1U;
  }
}

} // namespace

std::optional<ChunkSyncDelta> parse_chunk_sync(const nlohmann::json& snapshot_payload) {
  if (!snapshot_payload.is_object() || !snapshot_payload.contains("chunk_sync") ||
      !snapshot_payload["chunk_sync"].is_object()) {
    return std::nullopt;
  }

  ChunkSyncDelta delta{};
  const auto& chunk_sync = snapshot_payload["chunk_sync"];
  parse_array(chunk_sync, "added", &delta.added);
  parse_array(chunk_sync, "removed", &delta.removed);
  parse_array(chunk_sync, "deltas", &delta.deltas);
  return delta;
}

ChunkSyncApplyResult apply_chunk_sync(
    devy::voxel::World* world, int world_height, const ChunkSyncDelta& delta,
    std::unordered_map<devy::voxel::ChunkCoord, uint32_t, devy::voxel::ChunkCoordHash>* revisions) {
  ChunkSyncApplyResult result{};
  if (world == nullptr || revisions == nullptr) {
    return result;
  }

  for (const auto& entry : delta.removed) {
    const bool removed = world->remove_chunk(entry.coord.x, entry.coord.y, entry.coord.z);
    const std::size_t erased = revisions->erase(entry.coord);
    if (removed || erased > 0U) {
      result.changed = true;
      result.removed += 1U;
    }
  }

  apply_added_like(world, world_height, delta.added, revisions, &result);
  apply_added_like(world, world_height, delta.deltas, revisions, &result);
  return result;
}

} // namespace devy::client
