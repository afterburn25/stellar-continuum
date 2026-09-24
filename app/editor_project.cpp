#include "editor_project.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <stdexcept>

namespace stellar::editor {

std::string serialize_project(const EditorProject &project) {
  auto serialize_map = [](const std::unordered_map<int, SystemEdit> &map) {
    auto rows = nlohmann::json::array();
    for (const auto &[id, edit] : map) {
      if (edit.name.empty() && edit.note.empty() && !edit.bookmarked &&
          !edit.anomaly && !edit.rare_resource && !edit.pre_warp_civilization &&
          !edit.radius_earth && !edit.orbit_au && !edit.mass_earth)
        continue;
      auto row = nlohmann::json{{"id", id},
                                {"name", edit.name},
                                {"note", edit.note},
                                {"bookmarked", edit.bookmarked}};
      if (edit.anomaly) row["anomaly"] = *edit.anomaly;
      if (edit.rare_resource) row["rareResource"] = *edit.rare_resource;
      if (edit.pre_warp_civilization)
        row["preWarpCivilization"] = *edit.pre_warp_civilization;
      if (edit.radius_earth) row["radiusEarth"] = *edit.radius_earth;
      if (edit.orbit_au) row["orbitAu"] = *edit.orbit_au;
      if (edit.mass_earth) row["massEarth"] = *edit.mass_earth;
      if (edit.eccentricity) row["eccentricity"] = *edit.eccentricity;
      if (edit.inclination_degrees)
        row["inclinationDeg"] = *edit.inclination_degrees;
      if (edit.position_x) row["positionX"] = *edit.position_x;
      if (edit.position_y) row["positionY"] = *edit.position_y;
      rows.push_back(std::move(row));
    }
    return rows;
  };
  const nlohmann::json document{{"schemaVersion", 1},
                                {"seed", project.seed},
                                {"systems", project.system_count},
                                {"name", project.name},
                                {"edits", serialize_map(project.edits)},
                                {"bodyEdits", serialize_map(project.body_edits)}};
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
    auto parse_map = [](const nlohmann::json &rows,
                        std::unordered_map<int, SystemEdit> &out) {
      for (const auto &row : rows) {
        SystemEdit edit;
        edit.name = row.value("name", std::string{});
        edit.note = row.value("note", std::string{});
        edit.bookmarked = row.value("bookmarked", false);
        const auto flag = [](const nlohmann::json &row, const char *key) {
          const auto it = row.find(key);
          return it != row.end() && it->is_boolean()
                     ? std::optional<bool>{it->get<bool>()}
                     : std::nullopt;
        };
        edit.anomaly = flag(row, "anomaly");
        edit.rare_resource = flag(row, "rareResource");
        edit.pre_warp_civilization = flag(row, "preWarpCivilization");
        if (const auto it = row.find("radiusEarth");
            it != row.end() && it->is_number() && it->get<double>() > 0.)
          edit.radius_earth = it->get<double>();
        if (const auto it = row.find("orbitAu");
            it != row.end() && it->is_number() && it->get<double>() > 0.)
          edit.orbit_au = it->get<double>();
        if (const auto it = row.find("massEarth");
            it != row.end() && it->is_number() && it->get<double>() > 0.)
          edit.mass_earth = it->get<double>();
        // Eccentricity is the one override where zero is meaningful
        // (circular orbit); AnalyticOrbit rejects >= 0.95.
        if (const auto it = row.find("eccentricity");
            it != row.end() && it->is_number() && it->get<double>() >= 0. &&
            it->get<double>() < 0.95)
          edit.eccentricity = it->get<double>();
        // Inclination is bounded to the generated domain 0-180 degrees.
        if (const auto it = row.find("inclinationDeg");
            it != row.end() && it->is_number() && it->get<double>() >= 0. &&
            it->get<double>() <= 180.)
          edit.inclination_degrees = it->get<double>();
        // Map position axes are unbounded finite numbers — the galaxy is
        // centered on the origin so negatives are normal coordinates.
        const auto finite_number = [](const nlohmann::json &row,
                                      const char *key) {
          const auto it = row.find(key);
          return it != row.end() && it->is_number() &&
                         std::isfinite(it->get<double>())
                     ? std::optional<double>{it->get<double>()}
                     : std::nullopt;
        };
        edit.position_x = finite_number(row, "positionX");
        edit.position_y = finite_number(row, "positionY");
        if (!edit.name.empty() || !edit.note.empty() || edit.bookmarked ||
            edit.anomaly || edit.rare_resource || edit.pre_warp_civilization ||
            edit.radius_earth || edit.orbit_au || edit.mass_earth ||
            edit.eccentricity || edit.inclination_degrees ||
            edit.position_x || edit.position_y)
          out[row.at("id").get<int>()] = std::move(edit);
      }
    };
    parse_map(document.at("edits"), project.edits);
    // bodyEdits/name are additive within schemaVersion 1: older documents
    // omit them and older readers ignore unknown keys.
    if (const auto it = document.find("bodyEdits");
        it != document.end() && it->is_array())
      parse_map(*it, project.body_edits);
    if (const auto it = document.find("name");
        it != document.end() && it->is_string())
      project.name = it->get<std::string>();
    return project;
  } catch (const nlohmann::json::exception &error) {
    throw std::runtime_error(std::string("malformed project: ") + error.what());
  }
}

std::string sanitize_project_name(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  bool dash = true; // suppress leading dashes
  for (const unsigned char c : name) {
    if (std::isalnum(c)) {
      out += static_cast<char>(std::tolower(c));
      dash = false;
    } else if (!dash) {
      out += '-';
      dash = true;
    }
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out;
}

} // namespace stellar::editor
