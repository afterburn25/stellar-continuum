#include "editor_project.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace stellar::editor {

std::string serialize_project(const EditorProject &project) {
  auto edits = nlohmann::json::array();
  for (const auto &[id, edit] : project.edits) {
    if (edit.name.empty() && edit.note.empty() && !edit.bookmarked) continue;
    edits.push_back({{"id", id},
                     {"name", edit.name},
                     {"note", edit.note},
                     {"bookmarked", edit.bookmarked}});
  }
  const nlohmann::json document{{"schemaVersion", 1},
                                {"seed", project.seed},
                                {"systems", project.system_count},
                                {"edits", std::move(edits)}};
  return document.dump(2);
}

EditorProject parse_project(std::string_view text) {
  nlohmann::json document;
  try {
    document = nlohmann::json::parse(text);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("malformed project: ") + error.what());
  }
  try {
    if (document.at("schemaVersion").get<int>() != 1)
      throw std::runtime_error("unsupported editor project version");
    EditorProject project;
    project.seed = document.at("seed").get<std::int64_t>();
    project.system_count = document.at("systems").get<int>();
    for (const auto &row : document.at("edits")) {
      SystemEdit edit;
      edit.name = row.value("name", std::string{});
      edit.note = row.value("note", std::string{});
      edit.bookmarked = row.value("bookmarked", false);
      if (!edit.name.empty() || !edit.note.empty() || edit.bookmarked)
        project.edits[row.at("id").get<int>()] = std::move(edit);
    }
    return project;
  } catch (const nlohmann::json::exception &error) {
    throw std::runtime_error(std::string("malformed project: ") + error.what());
  }
}

} // namespace stellar::editor
