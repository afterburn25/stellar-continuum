#include <stellar/engine/draw_batcher.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace stellar::engine;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
} // namespace

int main() {
  // --- Draw batcher ---
  DrawBatcher batcher;
  batcher.begin_frame();
  // Opaque: two ships sharing material/mesh + one different.
  batcher.submit({1, 7, 0, 10.0f, 0, false});
  batcher.submit({1, 7, 1, 20.0f, 0, false});
  batcher.submit({2, 7, 2, 5.0f, 0, false});
  // Transparent: two sprites at different depths.
  batcher.submit({9, 3, 3, 100.0f, 1, true});
  batcher.submit({9, 3, 4, 50.0f, 1, true});
  // Culled item.
  batcher.submit({5, 5, 5, 1.0f, 0, false});
  batcher.cull(5);
  batcher.build();

  check(batcher.sorted_items().size() == 5, "culled item removed");
  // Opaque batching: mesh 1 items adjacent -> one batch of 2.
  check(batcher.batches().size() == 3,
        "shared mesh+material groups into one batch each");
  check(batcher.batches()[0].count == 2, "shared mesh+material batch");
  // Transparent sorted back-to-front (larger depth first).
  const auto &items = batcher.sorted_items();
  const auto last = items.back();
  check(last.transparent && last.depth == 50.0f,
        "transparent sorted back-to-front");

  // --- Virtualized list ---
  VirtualizedList list;
  list.row_count = 1000;
  list.row_height = 24.0f;
  list.viewport_height = 240.0f;
  list.scroll_to(24.0f * 100.0f);
  const auto range = list.visible_range();
  check(range.first == 100 && range.last <= 112,
        "visible window around scroll offset");
  check(range.content_height == 24000.0f, "content height");
  list.ensure_visible(0);
  check(list.scroll_offset == 0.0f, "ensure_visible scrolls up");
  list.ensure_visible(999);
  check(list.scroll_offset > 0.0f, "ensure_visible scrolls down");
  check(list.scroll_offset <= list.max_scroll(), "scroll clamped");

  // --- Table model ---
  TableModel table;
  table.set_columns({{"name", "COL_NAME", 200, true},
                     {"mass", "COL_MASS", 100, true}});
  table.set_rows({{"earth", {{"Earth", 0, false}, {"", 1.0, true}}},
                  {"jupiter", {{"Jupiter", 0, false}, {"", 318.0, true}}},
                  {"mars", {{"Mars", 0, false}, {"", 0.107, true}}}});
  check(table.display_rows().size() == 3, "all rows visible");
  table.sort_by("mass", false); // descending
  check(table.display_rows()[0]->first == "jupiter",
        "numeric sort descending");
  table.sort_by("name", true);
  check(table.display_rows()[0]->first == "earth",
        "text sort ascending");
  table.refilter("ma");
  check(table.display_rows().size() == 1 &&
            table.display_rows()[0]->first == "mars",
        "filter narrows rows");
  table.refilter("");
  check(table.display_rows().size() == 3, "clear filter restores");

  // --- Tree model ---
  TreeModel tree;
  tree.add("galaxy", "Galaxy");
  tree.add("sol", "Sol", "galaxy");
  tree.add("earth", "Earth", "sol");
  tree.add("luna", "Luna", "earth");
  tree.add("alpha", "Alpha Centauri", "galaxy");
  // Collapsed root shows only the galaxy.
  auto flat = tree.flattened();
  check(flat.size() == 1 && flat[0].first->id == "galaxy",
        "collapsed tree shows roots");
  tree.set_expanded("galaxy", true);
  flat = tree.flattened();
  check(flat.size() == 3, "expanded root shows children");
  tree.set_expanded("sol", true);
  tree.set_expanded("earth", true);
  flat = tree.flattened();
  check(flat.size() == 5, "nested expansion");
  check(flat[3].second == 3, "luna depth 3");

  if (failures == 0)
    std::cout << "Draw batcher and UI view-model tests passed\n";
  return failures == 0 ? 0 : 1;
}
