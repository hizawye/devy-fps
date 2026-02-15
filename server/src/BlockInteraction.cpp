#include "server/BlockInteraction.h"

#include <utility>

namespace devy::server {
namespace {

constexpr devy::voxel::BlockId kAirBlockId = 0U;

bool world_to_chunk_axis(int world_axis, int& chunk_axis) {
  if (world_axis < 0) {
    return false;
  }
  chunk_axis = world_axis / devy::voxel::kChunkSize;
  return true;
}

} // namespace

BlockInteraction::BlockInteraction(BlockInteractionConfig config)
    : config_(sanitize_config(std::move(config))) {}

BlockUpdateOutcome BlockInteraction::apply(const BlockUpdateCommand& command,
                                           devy::voxel::World& world) const {
  BlockUpdateOutcome outcome{};

  if (!is_valid_block_id(command.block_id)) {
    outcome.status = BlockUpdateStatus::InvalidBlockId;
    return outcome;
  }

  const auto current_block =
      world.block_at(command.world_x, command.world_y, command.world_z);
  if (!current_block.has_value()) {
    outcome.status = BlockUpdateStatus::OutOfBounds;
    return outcome;
  }

  outcome.previous_block_id = current_block.value();
  if (command.block_id == kAirBlockId && outcome.previous_block_id == kAirBlockId) {
    outcome.status = BlockUpdateStatus::ConflictAlreadyEmpty;
    return outcome;
  }

  if (command.block_id != kAirBlockId && outcome.previous_block_id != kAirBlockId) {
    if (outcome.previous_block_id == command.block_id) {
      outcome.status = BlockUpdateStatus::NoChange;
      return outcome;
    }
    outcome.status = BlockUpdateStatus::ConflictAlreadyOccupied;
    return outcome;
  }

  if (!world.set_block(command.world_x, command.world_y, command.world_z, command.block_id)) {
    outcome.status = BlockUpdateStatus::OutOfBounds;
    return outcome;
  }

  if (!world_to_chunk_axis(command.world_x, outcome.chunk_x) ||
      !world_to_chunk_axis(command.world_y, outcome.chunk_y) ||
      !world_to_chunk_axis(command.world_z, outcome.chunk_z)) {
    outcome.status = BlockUpdateStatus::OutOfBounds;
    return outcome;
  }

  outcome.status = BlockUpdateStatus::Applied;
  return outcome;
}

BlockInteractionConfig BlockInteraction::sanitize_config(BlockInteractionConfig config) {
  if (config.valid_block_ids.empty()) {
    config.valid_block_ids = {
        static_cast<devy::voxel::BlockId>(0U), static_cast<devy::voxel::BlockId>(1U),
        static_cast<devy::voxel::BlockId>(2U), static_cast<devy::voxel::BlockId>(3U)};
  } else {
    config.valid_block_ids.insert(kAirBlockId);
  }
  return config;
}

bool BlockInteraction::is_valid_block_id(devy::voxel::BlockId block_id) const {
  return config_.valid_block_ids.find(block_id) != config_.valid_block_ids.end();
}

const char* to_string(BlockUpdateStatus status) {
  switch (status) {
    case BlockUpdateStatus::Applied: return "applied";
    case BlockUpdateStatus::OutOfBounds: return "out_of_bounds";
    case BlockUpdateStatus::InvalidBlockId: return "invalid_block_id";
    case BlockUpdateStatus::ConflictAlreadyEmpty: return "conflict_already_empty";
    case BlockUpdateStatus::ConflictAlreadyOccupied: return "conflict_already_occupied";
    case BlockUpdateStatus::NoChange: return "no_change";
    default: return "unknown_block_update_status";
  }
}

} // namespace devy::server
