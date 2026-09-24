#include <stellar/engine/ui_viewmodels.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace stellar::engine {

VirtualizedList::Range VirtualizedList::visible_range() const {
  Range range;
  range.content_height = row_height * static_cast<float>(row_count);
  if (row_count == 0 || row_height <= 0.0f || viewport_height <= 0.0f)
    return range;
  const auto first = static_cast<std::size_t>(
      std::clamp(std::floor(scroll_offset / row_height), 0.0f,
                 static_cast<float>(row_count - 1)));
  const auto visible = static_cast<std::size_t>(
      std::ceil(viewport_height / row_height)) + 1; // one row of overscan
  range.first = first;
  range.last = std::min(row_count, first + visible);
  return range;
}

void VirtualizedList::ensure_visible(std::size_t row) {
  if (row >= row_count)
    return;
  const auto top = row_height * static_cast<float>(row);
  const auto bottom = top + row_height;
  if (top < scroll_offset)
    scroll_to(top);
  else if (bottom > scroll_offset + viewport_height)
    scroll_to(bottom - viewport_height);
}

float VirtualizedList::max_scroll() const {
  return std::max(0.0f, row_height * static_cast<float>(row_count) -
                            viewport_height);
}

void VirtualizedList::scroll_to(float offset) {
  scroll_offset = std::clamp(offset, 0.0f, max_scroll());
}

void VirtualizedList::configure(std::size_t rows, float new_row_height,
                                float new_viewport_height) {
  row_count = rows;
  row_height = new_row_height;
  viewport_height = new_viewport_height;
  scroll_to(scroll_offset);
}

void VirtualizedList::set_row_count(std::size_t rows) {
  row_count = rows;
  scroll_to(scroll_offset);
}

std::size_t VirtualizedList::sync_rows(std::size_t rows, float new_row_height,
                                       float new_viewport_height) {
  configure(rows, new_row_height, new_viewport_height);
  if (row_height > 0.0f)
    scroll_offset =
        std::floor(scroll_offset / row_height) * row_height;
  return row_height > 0.0f
             ? static_cast<std::size_t>(scroll_offset / row_height)
             : 0;
}

void TableModel::set_columns(std::vector<TableColumn> columns) {
  columns_ = std::move(columns);
}
void TableModel::set_rows(std::vector<Row> rows) {
  rows_ = std::move(rows);
  rebuild_display();
}

void TableModel::sort_by(std::string_view column_id, bool ascending) {
  sort_state_ = std::pair{std::string(column_id), ascending};
  rebuild_display();
}

void TableModel::refilter(std::string_view needle) {
  filter_.assign(needle);
  std::transform(filter_.begin(), filter_.end(), filter_.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  rebuild_display();
}

void TableModel::rebuild_display() {
  display_.clear();
  std::size_t sort_column = columns_.size();
  if (sort_state_)
    for (std::size_t i = 0; i < columns_.size(); ++i)
      if (columns_[i].id == sort_state_->first) {
        sort_column = i;
        break;
      }
  for (const auto &row : rows_) {
    if (!filter_.empty()) {
      bool hit = false;
      for (const auto &cell : row.second) {
        auto text = cell.text;
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (text.find(filter_) != std::string::npos) {
          hit = true;
          break;
        }
      }
      if (!hit)
        continue;
    }
    display_.push_back(&row);
  }
  if (sort_column < columns_.size()) {
    const auto ascending = sort_state_->second;
    std::stable_sort(display_.begin(), display_.end(),
                     [&](const Row *a, const Row *b) {
                       const Cell empty{};
                       const auto &ca = sort_column < a->second.size()
                                            ? a->second[sort_column]
                                            : empty;
                       const auto &cb = sort_column < b->second.size()
                                            ? b->second[sort_column]
                                            : empty;
                       if (ca.is_numeric && cb.is_numeric && ca.numeric != cb.numeric)
                         return ascending ? ca.numeric < cb.numeric
                                          : ca.numeric > cb.numeric;
                       if (ca.text != cb.text)
                         return ascending ? ca.text < cb.text
                                          : ca.text > cb.text;
                       return a->first < b->first; // stable tie-break
                     });
  }
}

TreeModel::Node &TreeModel::add(std::string id, std::string label_key,
                                std::string parent_id) {
  Node node;
  node.id = std::move(id);
  node.label_key = std::move(label_key);
  node.parent_id = parent_id;
  if (parent_id.empty())
    roots_.push_back(node.id);
  else if (auto *parent = find_node(parent_id))
    parent->children.push_back(node.id);
  nodes_.push_back(std::move(node));
  return nodes_.back();
}

TreeModel::Node *TreeModel::find_node(std::string_view id) {
  for (auto &node : nodes_)
    if (node.id == id)
      return &node;
  return nullptr;
}
TreeModel::Node *TreeModel::find(std::string_view id) {
  return find_node(id);
}
const TreeModel::Node *TreeModel::find(std::string_view id) const {
  for (const auto &node : nodes_)
    if (node.id == id)
      return &node;
  return nullptr;
}

void TreeModel::set_expanded(std::string_view id, bool expanded) {
  if (auto *node = find_node(id))
    node->expanded = expanded;
}
bool TreeModel::is_expanded(std::string_view id) const {
  const auto *node = find(id);
  return node != nullptr && node->expanded;
}

void TreeModel::select(std::string_view id) {
  selected_ = find(id) != nullptr ? std::string(id) : std::string{};
}
const TreeModel::Node *TreeModel::selected() const {
  return selected_.empty() ? nullptr : find(selected_);
}
bool TreeModel::move_selection(int delta) {
  if (delta == 0)
    return false;
  const auto flat = flattened();
  for (std::size_t i = 0; i < flat.size(); ++i) {
    if (flat[i].first->id != selected_)
      continue;
    const auto next = static_cast<std::ptrdiff_t>(i) + delta;
    if (next < 0 || next >= static_cast<std::ptrdiff_t>(flat.size()))
      return false;
    selected_ = flat[static_cast<std::size_t>(next)].first->id;
    return true;
  }
  selected_.clear();
  return false;
}

std::vector<std::pair<const TreeModel::Node *, int>>
TreeModel::flattened() const {
  std::vector<std::pair<const Node *, int>> result;
  std::vector<std::pair<std::string, int>> stack;
  for (auto it = roots_.rbegin(); it != roots_.rend(); ++it)
    stack.emplace_back(*it, 0);
  while (!stack.empty()) {
    const auto [id, depth] = stack.back();
    stack.pop_back();
    const auto *node = find(id);
    if (node == nullptr)
      continue;
    result.emplace_back(node, depth);
    if (node->expanded)
      for (auto it = node->children.rbegin(); it != node->children.rend();
           ++it)
        stack.emplace_back(*it, depth + 1);
  }
  return result;
}

} // namespace stellar::engine
