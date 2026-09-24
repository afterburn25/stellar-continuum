#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// Reusable UI view-model primitives. These are renderer-agnostic data
// structures: the native UI binds them to drawing code. They centralize the
// behavior every list/table/tree needs — virtualization windows, selection,
// sorting, expansion — so screens don't reimplement scrolling state.

// A windowed view over N rows: given scroll offset and viewport height, the
// model reports which row range must be rendered.
struct VirtualizedList {
  std::size_t row_count{};
  float row_height{24.0f};
  float scroll_offset{};
  float viewport_height{};

  struct Range {
    std::size_t first{}, last{}; // [first, last) visible row indices
    float content_height{};
  };
  Range visible_range() const;
  // Scrolls so `row` is inside the viewport.
  void ensure_visible(std::size_t row);
  float max_scroll() const;
  void scroll_to(float offset);
  // Applies the row count and viewport geometry then re-clamps
  // scroll_offset against the new maximum — call whenever the row source
  // or viewport changes so a shrink cannot strand the scroll past the
  // tail. Fractional offsets are preserved.
  void configure(std::size_t rows, float row_height,
                 float viewport_height);
  // Sets the row count and re-clamps scroll_offset — for row-source
  // shrinks (filters, rebuilds) where the geometry is unchanged.
  void set_row_count(std::size_t rows);
  // Frame sync for row-snapped consumers: reconfigures the model,
  // re-clamps the offset against the new content (a shrinking row set
  // can never strand it past the tail), snaps down to a whole-row
  // boundary, and returns the first visible row index.
  std::size_t sync_rows(std::size_t rows, float row_height,
                      float viewport_height);
};

// Pixel-offset scroll model for variable-height content (wrapped text,
// card lists, mixed blocks). The consumer reports the laid-out content
// height and viewport each frame; the model clamps the offset into
// [0, max_scroll] and provides scrollbar thumb geometry. Fixed-stride
// row lists should use VirtualizedList instead.
struct ScrollView {
  float content_height{};
  float viewport_height{};
  float scroll_offset{};

  [[nodiscard]] float max_scroll() const;
  // Clamps `offset` into [0, max_scroll]; a non-finite offset resets to 0.
  void scroll_to(float offset);
  // Applies a signed delta (wheel ticks, drag distance) via scroll_to.
  void scroll_by(float delta);
  // Applies freshly laid-out geometry and re-clamps the offset so a
  // shrinking content set or a growing viewport cannot strand it past
  // the tail. Returns the clamped offset.
  float sync(float new_content_height, float new_viewport_height);

  struct Thumb {
    float offset{}, size{};
  };
  // Scrollbar thumb over a `track`-pixel rail: size is proportional
  // (track * viewport / content), floored at `min_size`, capped at the
  // track; {0,0} when the content fits the viewport.
  [[nodiscard]] Thumb thumb(float track, float min_size) const;
};

struct TableColumn {
  std::string id;
  std::string title_key; // localization key
  float width{120.0f};
  bool sortable{true};
};

// Row model with stable sort (deterministic ties by row id).
class TableModel {
public:
  struct Cell {
    std::string text;
    double numeric{};
    bool is_numeric{};
  };
  using Row = std::pair<std::string, std::vector<Cell>>; // id -> cells

  void set_columns(std::vector<TableColumn> columns);
  void set_rows(std::vector<Row> rows);

  const std::vector<TableColumn> &columns() const { return columns_; }
  // Rows in display order (post-sort).
  const std::vector<const Row *> &display_rows() const { return display_; }

  void sort_by(std::string_view column_id, bool ascending);
  const std::optional<std::pair<std::string, bool>> &sort_state() const {
    return sort_state_;
  }
  void refilter(std::string_view needle); // case-insensitive contains
  std::string_view filter() const { return filter_; }

private:
  void rebuild_display();

  std::vector<TableColumn> columns_;
  std::vector<Row> rows_;
  std::vector<const Row *> display_;
  std::optional<std::pair<std::string, bool>> sort_state_;
  std::string filter_;
};

// Expandable tree with stable node ids.
class TreeModel {
public:
  struct Node {
    std::string id;
    std::string parent_id; // empty = root
    std::string label_key;
    bool expanded{};
    std::vector<std::string> children; // insertion order preserved
  };

  Node &add(std::string id, std::string label_key,
            std::string parent_id = {});
  Node *find(std::string_view id);
  const Node *find(std::string_view id) const;

  void set_expanded(std::string_view id, bool expanded);
  bool is_expanded(std::string_view id) const;

  // Selection, stable by node id. select() clears when the id is absent,
  // so a consumer can re-apply a persisted id after a rebuild.
  void select(std::string_view id);
  void clear_selection() { selected_.clear(); }
  const Node *selected() const;
  std::string_view selected_id() const { return selected_; }
  // Moves the selection delta rows through the flattened (visible) view;
  // clears a selection that is not currently visible. Returns true when
  // the selection changed.
  bool move_selection(int delta);

  // Depth-first flattened view: visible rows (a node appears only when all
  // ancestors are expanded). Pair = (node, depth).
  std::vector<std::pair<const Node *, int>> flattened() const;

private:
  std::vector<Node> nodes_;                       // stable by pointer? no —
  std::vector<std::string> roots_;                // ids only
  std::string selected_;
  Node *find_node(std::string_view id);
};

} // namespace stellar::engine
