#include "shared/game/Inventory.h"

namespace devy::game {

void Inventory::add_treasure(const TreasureDefinition& treasure) {
  items_.push_back(treasure);
}

int Inventory::total_value() const {
  int total = 0;
  for (const auto& item : items_) {
    total += item.value;
  }
  return total;
}

int Inventory::total_weight() const {
  int total = 0;
  for (const auto& item : items_) {
    total += item.weight;
  }
  return total;
}

const std::vector<TreasureDefinition>& Inventory::items() const {
  return items_;
}

} // namespace devy::game
