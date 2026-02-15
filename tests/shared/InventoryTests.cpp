#include "shared/game/Inventory.h"

#include <catch2/catch_test_macros.hpp>

namespace devy::game {
namespace {

TEST_CASE("Inventory totals are accumulated from all treasure items") {
  Inventory inv;
  inv.add_treasure({"coins", 10, 1, "common"});
  inv.add_treasure({"artifact", 250, 6, "epic"});

  REQUIRE(inv.items().size() == 2);
  REQUIRE(inv.total_value() == 260);
  REQUIRE(inv.total_weight() == 7);
}

} // namespace
} // namespace devy::game
