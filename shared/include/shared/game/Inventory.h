#pragma once

#include "shared/game/Treasure.h"

#include <vector>

namespace devy::game {

class Inventory {
public:
  void add_treasure(const TreasureDefinition& treasure);
  int total_value() const;
  int total_weight() const;
  const std::vector<TreasureDefinition>& items() const;

private:
  std::vector<TreasureDefinition> items_;
};

} // namespace devy::game
