#pragma once

#include "shared/voxel/Chunk.h"
#include "shared/voxel/World.h"

#include <cstdint>
#include <unordered_set>

namespace devy::server {

struct BlockInteractionConfig {
  std::unordered_set<devy::voxel::BlockId> valid_block_ids{};
};

enum class BlockUpdateStatus : uint8_t {
  Applied = 0,
  OutOfBounds,
  InvalidBlockId,
  ConflictAlreadyEmpty,
  ConflictAlreadyOccupied,
  NoChange
};

struct BlockUpdateCommand {
  uint32_t player_id{0};
  int world_x{0};
  int world_y{0};
  int world_z{0};
  devy::voxel::BlockId block_id{0};
};

struct BlockUpdateOutcome {
  BlockUpdateStatus status{BlockUpdateStatus::OutOfBounds};
  devy::voxel::BlockId previous_block_id{0};
  int chunk_x{0};
  int chunk_y{0};
  int chunk_z{0};

  [[nodiscard]] bool applied() const {
    return status == BlockUpdateStatus::Applied;
  }
};

class BlockInteraction {
 public:
  explicit BlockInteraction(BlockInteractionConfig config = {});

  [[nodiscard]] BlockUpdateOutcome apply(const BlockUpdateCommand& command,
                                         devy::voxel::World& world) const;

 private:
  [[nodiscard]] static BlockInteractionConfig sanitize_config(BlockInteractionConfig config);
  [[nodiscard]] bool is_valid_block_id(devy::voxel::BlockId block_id) const;

  BlockInteractionConfig config_{};
};

const char* to_string(BlockUpdateStatus status);

} // namespace devy::server
