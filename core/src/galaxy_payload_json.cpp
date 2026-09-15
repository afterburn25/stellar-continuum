#include <stellar/core/galaxy_payload_json.hpp>

#include "json_ordered_value.hpp"
#include "galaxy_payload_json_internal.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <ranges>
#include <sstream>
#include <type_traits>
#include <utility>

namespace stellar::core {
namespace {

using Value = json_detail::Value;
using Object = Value::Object;
using Array = Value::Array;
using Json = nlohmann::ordered_json;

[[noreturn]] void json_error(GalaxyPayloadJsonErrorPhase phase,
                             const Value &value, const std::string &path,
                             std::string message) {
  throw GalaxyPayloadJsonError(phase, std::move(message), path, value.line,
                               value.byte);
}

const Object &object(const Value &value, const std::string &path) {
  if (const auto found = std::get_if<Object>(&value.data))
    return *found;
  json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
             "The JSON value could not be converted to an object.");
}

const Array &array(const Value &value, const std::string &path) {
  if (const auto found = std::get_if<Array>(&value.data))
    return *found;
  json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
             "The JSON value could not be converted to an array.");
}

bool is_null(const Value &value) {
  return std::holds_alternative<std::nullptr_t>(value.data);
}

bool boolean(const Value &value, const std::string &path) {
  if (const auto found = std::get_if<bool>(&value.data))
    return *found;
  json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
             "The JSON value could not be converted to Boolean.");
}

std::string string(const Value &value, const std::string &path) {
  if (const auto found = std::get_if<std::string>(&value.data))
    return *found;
  if (is_null(value))
    json_error(GalaxyPayloadJsonErrorPhase::Representability, value, path,
               "System.Text.Json accepted null for a string that the native DTO stores as a value.");
  json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
             "The JSON value could not be converted to String.");
}

template <typename Integer>
Integer integer(const Value &value, const std::string &path) {
  static_assert(std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>);
  const auto found = std::get_if<Value::Number>(&value.data);
  if (!found || found->text.find_first_of(".eE") != std::string::npos)
    json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
               "The JSON number could not be converted to the requested integer width.");
  Integer result{};
  const auto parsed = std::from_chars(found->text.data(),
                                      found->text.data() + found->text.size(),
                                      result);
  if (parsed.ec != std::errc{} ||
      parsed.ptr != found->text.data() + found->text.size())
    json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
               "The JSON number is outside the requested integer width.");
  return result;
}

template <typename Number>
Number floating_number(const Value &value, const std::string &path,
                       std::string_view source_type) {
  const auto found = std::get_if<Value::Number>(&value.data);
  if (!found)
    json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
               "The JSON value could not be converted to " +
                   std::string(source_type) + '.');
  Number result{};
  const auto parsed = std::from_chars(found->text.data(),
                                      found->text.data() + found->text.size(),
                                      result, std::chars_format::general);
  if (parsed.ec == std::errc{} &&
      parsed.ptr == found->text.data() + found->text.size())
    return result;
  if (parsed.ec != std::errc::result_out_of_range)
    json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
               "The JSON number could not be converted to " +
                   std::string(source_type) + '.');

  const auto sign_offset =
      !found->text.empty() && found->text.front() == '-' ? 1U : 0U;
  const auto exponent_position = found->text.find_first_of("eE");
  const auto significand_end = exponent_position == std::string::npos
                                   ? found->text.size()
                                   : exponent_position;
  const auto decimal_position = found->text.find('.', sign_offset);
  const auto integral_digits =
      (decimal_position == std::string::npos ||
       decimal_position > significand_end)
          ? significand_end - sign_offset
          : decimal_position - sign_offset;
  std::size_t first_nonzero{};
  bool found_nonzero{};
  for (std::size_t index = sign_offset; index < significand_end; ++index) {
    if (found->text[index] == '.')
      continue;
    if (found->text[index] != '0') {
      found_nonzero = true;
      break;
    }
    ++first_nonzero;
  }
  const bool negative = !found->text.empty() && found->text.front() == '-';
  if (!found_nonzero)
    return negative ? -Number{} : Number{};

  constexpr std::int64_t order_limit = 1'000'000;
  std::int64_t explicit_exponent{};
  if (exponent_position != std::string::npos) {
    auto exponent_text = std::string_view(found->text).substr(
        exponent_position + 1);
    const bool exponent_negative =
        !exponent_text.empty() && exponent_text.front() == '-';
    if (!exponent_text.empty() &&
        (exponent_text.front() == '+' || exponent_text.front() == '-'))
      exponent_text.remove_prefix(1);
    for (const auto digit : exponent_text) {
      if (explicit_exponent > (order_limit - (digit - '0')) / 10) {
        explicit_exponent = order_limit;
        break;
      }
      explicit_exponent = explicit_exponent * 10 + (digit - '0');
    }
    if (exponent_negative) explicit_exponent = -explicit_exponent;
  }
  std::int64_t significand_order{};
  if (integral_digits > first_nonzero) {
    significand_order = static_cast<std::int64_t>(
        std::min<std::size_t>(integral_digits - first_nonzero - 1,
                              order_limit));
  } else {
    significand_order = -static_cast<std::int64_t>(
        std::min<std::size_t>(first_nonzero - integral_digits + 1,
                              order_limit));
  }
  const auto decimal_order =
      explicit_exponent > order_limit - significand_order
          ? order_limit
          : explicit_exponent < -order_limit - significand_order
                ? -order_limit
                : explicit_exponent + significand_order;
  const bool underflow = decimal_order < 0;
  if (underflow)
    return negative ? -Number{} : Number{};
  return negative ? -std::numeric_limits<Number>::infinity()
                  : std::numeric_limits<Number>::infinity();
}

double number(const Value &value, const std::string &path) {
  return floating_number<double>(value, path, "Double");
}

float single(const Value &value, const std::string &path) {
  return floating_number<float>(value, path, "Single");
}

template <typename T, typename Decode>
std::optional<T> optional_value(const Value &value, const std::string &path,
                                Decode decode) {
  if (is_null(value))
    return std::nullopt;
  return decode(value, path);
}

template <typename T, typename Decode>
std::vector<T> list(const Value &value, const std::string &path, Decode decode,
                    bool nullable_elements = false) {
  std::vector<T> result;
  const auto &items = array(value, path);
  result.reserve(items.size());
  for (std::size_t index = 0; index != items.size(); ++index) {
    const auto item_path = path + '[' + std::to_string(index) + ']';
    if (is_null(items[index]) && !nullable_elements)
      json_error(GalaxyPayloadJsonErrorPhase::Representability, items[index],
                 item_path,
                 "System.Text.Json accepted a null list element that the native DTO cannot retain.");
    result.push_back(decode(items[index], item_path));
  }
  return result;
}

template <typename T, typename Decode>
std::optional<std::vector<T>> optional_list(const Value &value,
                                            const std::string &path,
                                            Decode decode) {
  if (is_null(value))
    return std::nullopt;
  return list<T>(value, path, decode);
}

std::string canonical_datetime_offset(const Value &value,
                                      const std::string &path) {
  const auto text = string(value, path);
  const auto decimal_at = [&](std::size_t offset, std::size_t count) {
    if (offset + count > text.size()) return -1;
    int result{};
    for (std::size_t index = 0; index != count; ++index) {
      const auto character = text[offset + index];
      if (character < '0' || character > '9') return -1;
      result = result * 10 + character - '0';
    }
    return result;
  };
  const bool local_shape =
      (text.size() == 10 || text.size() == 19) && text[4] == '-' &&
      text[7] == '-' &&
      (text.size() == 10 ||
       (text[10] == 'T' && text[13] == ':' && text[16] == ':'));
  if (local_shape) {
    const auto local_year = decimal_at(0, 4);
    const auto local_month = decimal_at(5, 2);
    const auto local_day = decimal_at(8, 2);
    const auto local_hour = text.size() == 19 ? decimal_at(11, 2) : 0;
    const auto local_minute = text.size() == 19 ? decimal_at(14, 2) : 0;
    const auto local_second = text.size() == 19 ? decimal_at(17, 2) : 0;
    const auto local_leap = local_year % 4 == 0 &&
                            (local_year % 100 != 0 || local_year % 400 == 0);
    constexpr int local_month_days[] = {0, 31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};
    if (local_year > 0 && local_month >= 1 && local_month <= 12 &&
        local_day >= 1 &&
        local_day <= local_month_days[local_month] +
                         (local_month == 2 && local_leap ? 1 : 0) &&
        local_hour >= 0 && local_hour <= 23 && local_minute >= 0 &&
        local_minute <= 59 && local_second >= 0 && local_second <= 59)
      json_error(GalaxyPayloadJsonErrorPhase::Representability, value, path,
                 "System.Text.Json interpreted a local DateTimeOffset using the host time zone, which this portable codec cannot retain deterministically.");
  }
  auto failure = [&] {
    json_error(GalaxyPayloadJsonErrorPhase::DateTimeOffset, value, path,
               "The JSON value is not a valid DateTimeOffset.");
  };
  if (text.size() < 20)
    failure();
  auto digits = [&](std::size_t offset, std::size_t count) {
    int result{};
    for (std::size_t index = 0; index != count; ++index) {
      const auto character = text[offset + index];
      if (character < '0' || character > '9')
        failure();
      result = result * 10 + character - '0';
    }
    return result;
  };
  if (text[4] != '-' || text[7] != '-' ||
      text[10] != 'T' || text[13] != ':' ||
      text[16] != ':')
    failure();
  const auto year = digits(0, 4);
  const auto month = digits(5, 2);
  const auto day = digits(8, 2);
  const auto hour = digits(11, 2);
  const auto minute = digits(14, 2);
  const auto second = digits(17, 2);
  const auto leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  constexpr int month_days[] = {0, 31, 28, 31, 30, 31, 30,
                                31, 31, 30, 31, 30, 31};
  if (year == 0 || month < 1 || month > 12 || day < 1 ||
      day > month_days[month] + (month == 2 && leap ? 1 : 0) || hour > 23 ||
      minute > 59 || second > 59)
    failure();
  std::size_t cursor = 19;
  std::string fraction;
  if (cursor < text.size() && text[cursor] == '.') {
    ++cursor;
    const auto start = cursor;
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
      ++cursor;
    if (cursor == start || cursor - start > 16)
      failure();
    fraction = text.substr(start, std::min<std::size_t>(7, cursor - start));
    while (fraction.size() < 7)
      fraction.push_back('0');
    while (!fraction.empty() && fraction.back() == '0')
      fraction.pop_back();
  }
  int offset_hour{};
  int offset_minute{};
  char offset_sign = '+';
  if (cursor == text.size() - 1 &&
      text[cursor] == 'Z') {
    ++cursor;
  } else {
    if (cursor + 6 != text.size() ||
        (text[cursor] != '+' && text[cursor] != '-') ||
        text[cursor + 3] != ':')
      failure();
    offset_sign = text[cursor];
    offset_hour = digits(cursor + 1, 2);
    offset_minute = digits(cursor + 4, 2);
    cursor += 6;
    if (offset_hour > 14 || offset_minute > 59 ||
        (offset_hour == 14 && offset_minute != 0))
      failure();
  }
  if (cursor != text.size())
    failure();
  const auto seconds_of_day = hour * 3600 + minute * 60 + second;
  const auto offset_seconds = offset_hour * 3600 + offset_minute * 60;
  if (year == 1 && month == 1 && day == 1 && offset_sign == '+' &&
      offset_seconds > seconds_of_day)
    failure();
  if (year == 9999 && month == 12 && day == 31 && offset_sign == '-' &&
      offset_seconds > 86399 - seconds_of_day)
    failure();
  if (offset_hour == 0 && offset_minute == 0)
    offset_sign = '+';
  char result[64]{};
  const auto length = std::snprintf(
      result, sizeof(result), "%04d-%02d-%02dT%02d:%02d:%02d", year, month,
      day, hour, minute, second);
  std::string canonical(result, static_cast<std::size_t>(length));
  if (!fraction.empty())
    canonical += '.' + fraction;
  canonical.push_back(offset_sign);
  char offset[8]{};
  std::snprintf(offset, sizeof(offset), "%02d:%02d", offset_hour,
                offset_minute);
  canonical += offset;
  return canonical;
}

std::string child(const std::string &path, std::string_view name) {
  return path + '.' + std::string(name);
}

template <typename Enum>
Enum enumeration(const Value &value, const std::string &path) {
  return static_cast<Enum>(integer<int>(value, path));
}

GalacticCoreMetadata decode_core_value(const Value &value,
                                       const std::string &path) {
  GalacticCoreMetadata result;
  bool landmark_key{};
  for (const auto &[name, member] : object(value, path)) {
    const auto member_path = child(path, name);
    if (name == "LandmarkKey") {
      result.landmark_key = string(member, member_path);
      landmark_key = true;
    }
    else if (name == "X") result.x = single(member, member_path);
    else if (name == "Y") result.y = single(member, member_path);
    else if (name == "ExclusionRadius") result.exclusion_radius = single(member, member_path);
  }
  if (!landmark_key)
    json_error(GalaxyPayloadJsonErrorPhase::Representability, value,
               child(path, "LandmarkKey"),
               "System.Text.Json produced a null landmark key that the native DTO cannot retain.");
  return result;
}

std::optional<GalacticCoreMetadata> decode_core(const Value &value,
                                                const std::string &path) {
  return optional_value<GalacticCoreMetadata>(value, path, decode_core_value);
}

GalaxyGenerationMetadata decode_metadata_value(const Value &value,
                                                const std::string &path) {
  GalaxyGenerationMetadata result;
  result.created_at_utc = "0001-01-01T00:00:00+00:00";
  result.art_profile_version = "legacy-static-v1";
  result.anomaly_frequency = "Standard";
  bool entered_seed{}, generator_version{}, galaxy_shape{}, stellar_variety{};
  bool planet_bearing{}, habitable_worlds{}, ancient{}, hazards{};
  bool development{}, difficulty{};
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "EnteredSeed") { result.entered_seed = string(member, p); entered_seed = true; }
    else if (name == "InternalSeed") result.internal_seed = integer<std::int64_t>(member, p);
    else if (name == "GeneratorVersion") { result.generator_version = string(member, p); generator_version = true; }
    else if (name == "CreatedAtUtc") result.created_at_utc = canonical_datetime_offset(member, p);
    else if (name == "SystemCount") result.system_count = integer<int>(member, p);
    else if (name == "GalaxyShape") { result.galaxy_shape = string(member, p); galaxy_shape = true; }
    else if (name == "StellarVariety") { result.stellar_variety = string(member, p); stellar_variety = true; }
    else if (name == "PlanetBearingSystems") { result.planet_bearing_systems = string(member, p); planet_bearing = true; }
    else if (name == "HabitableWorlds") { result.habitable_worlds = string(member, p); habitable_worlds = true; }
    else if (name == "GuaranteedNearbyHabitableWorlds") result.guaranteed_nearby_habitable_worlds = integer<int>(member, p);
    else if (name == "OtherCivilizations") result.other_civilizations = integer<int>(member, p);
    else if (name == "AncientCivilizations") { result.ancient_civilizations = string(member, p); ancient = true; }
    else if (name == "SpaceHazards") { result.space_hazards = string(member, p); hazards = true; }
    else if (name == "StartingDevelopment") { result.starting_development = string(member, p); development = true; }
    else if (name == "Difficulty") { result.difficulty = string(member, p); difficulty = true; }
    else if (name == "ArtProfileVersion") result.art_profile_version = string(member, p);
    else if (name == "PlayerSpeciesId") result.player_species_id = optional_value<std::string>(member, p, string);
    else if (name == "AnomalyFrequency") result.anomaly_frequency = string(member, p);
    else if (name == "GalacticCore") result.galactic_core = decode_core(member, p);
  }
  const std::array missing{
      std::pair{entered_seed, "EnteredSeed"},
      std::pair{generator_version, "GeneratorVersion"},
      std::pair{galaxy_shape, "GalaxyShape"},
      std::pair{stellar_variety, "StellarVariety"},
      std::pair{planet_bearing, "PlanetBearingSystems"},
      std::pair{habitable_worlds, "HabitableWorlds"},
      std::pair{ancient, "AncientCivilizations"},
      std::pair{hazards, "SpaceHazards"},
      std::pair{development, "StartingDevelopment"},
      std::pair{difficulty, "Difficulty"}};
  for (const auto &[present, name] : missing)
    if (!present)
      json_error(GalaxyPayloadJsonErrorPhase::Representability, value,
                 child(path, name),
                 "System.Text.Json produced a null metadata string that the native DTO cannot retain.");
  return result;
}

std::optional<GalaxyGenerationMetadata> decode_metadata(const Value &value,
                                                        const std::string &path) {
  return optional_value<GalaxyGenerationMetadata>(value, path,
                                                   decode_metadata_value);
}

StellarSystemPersistenceDto decode_system(const Value &value,
                                          const std::string &path) {
  StellarSystemPersistenceDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = integer<int>(member, p);
    else if (name == "Name") result.name = string(member, p);
    else if (name == "X") result.x = single(member, p);
    else if (name == "Y") result.y = single(member, p);
    else if (name == "GalacticDepthLightYears") result.galactic_depth_light_years = optional_value<double>(member, p, number);
    else if (name == "StellarCatalogId") result.stellar_catalog_id = optional_value<std::string>(member, p, string);
    else if (name == "Archetype") result.archetype = enumeration<StarArchetype>(member, p);
    else if (name == "HasHabitableWorld") result.has_habitable_world = boolean(member, p);
    else if (name == "HasAnomaly") result.has_anomaly = boolean(member, p);
    else if (name == "HasRareResource") result.has_rare_resource = boolean(member, p);
    else if (name == "HasPreWarpCivilization") result.has_pre_warp_civilization = boolean(member, p);
    else if (name == "CatalogPresetId") result.catalog_preset_id = optional_value<std::string>(member, p, string);
    else if (name == "StellarClass") result.stellar_class = optional_value<StellarClass>(member, p, enumeration<StellarClass>);
    else if (name == "SecondaryStellarClass") result.secondary_stellar_class = optional_value<StellarClass>(member, p, enumeration<StellarClass>);
    else if (name == "TertiaryStellarClass") result.tertiary_stellar_class = optional_value<StellarClass>(member, p, enumeration<StellarClass>);
  }
  return result;
}

PlanetaryEnvironmentPersistenceDto decode_environment(
    const Value &value, const std::string &path) {
  PlanetaryEnvironmentPersistenceDto result;
  bool gravity{}, temperature{}, pressure{}, atmosphere{}, solvent{}, radiation{}, immersed{}, surface{};
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "GravityG") { result.gravity_g = number(member, p); gravity = true; }
    else if (name == "TemperatureKelvin") { result.temperature_kelvin = number(member, p); temperature = true; }
    else if (name == "PressureKPa") { result.pressure_kpa = number(member, p); pressure = true; }
    else if (name == "Atmosphere") { result.atmosphere = enumeration<PlanetaryAtmosphereRegime>(member, p); atmosphere = true; }
    else if (name == "AvailableSolvent") { result.available_solvent = enumeration<PlanetarySolventRegime>(member, p); solvent = true; }
    else if (name == "RadiationHazard") { result.radiation_hazard = number(member, p); radiation = true; }
    else if (name == "IsImmersedEnvironment") { result.is_immersed_environment = boolean(member, p); immersed = true; }
    else if (name == "HasSolidSurface") { result.has_solid_surface = boolean(member, p); surface = true; }
  }
  if (!(gravity && temperature && pressure && atmosphere && solvent && radiation && immersed && surface))
    json_error(GalaxyPayloadJsonErrorPhase::RequiredMember, value, path,
               "JSON deserialization was missing required planetary environment properties.");
  return result;
}

PlanetaryBodyPersistenceDto decode_body(const Value &value,
                                        const std::string &path) {
  PlanetaryBodyPersistenceDto result;
  bool id{}, system{}, parent{}, orbit{}, name_present{}, kind{}, radius{}, mass{}, environment{}, legacy{}, rare{}, anomaly{}, prewarp{};
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") { result.id = integer<int>(member, p); id = true; }
    else if (name == "SystemId") { result.system_id = integer<int>(member, p); system = true; }
    else if (name == "ParentBodyId") { result.parent_body_id = optional_value<int>(member, p, integer<int>); parent = true; }
    else if (name == "OrbitIndex") { result.orbit_index = integer<int>(member, p); orbit = true; }
    else if (name == "Name") { result.name = optional_value<std::string>(member, p, string); name_present = true; }
    else if (name == "Kind") { result.kind = enumeration<PlanetaryBodyKind>(member, p); kind = true; }
    else if (name == "RadiusEarth") { result.radius_earth = number(member, p); radius = true; }
    else if (name == "MassEarth") { result.mass_earth = number(member, p); mass = true; }
    else if (name == "Environment") { result.environment = optional_value<PlanetaryEnvironmentPersistenceDto>(member, p, decode_environment); environment = true; }
    else if (name == "LegacyColonizationCandidate") { result.legacy_colonization_candidate = boolean(member, p); legacy = true; }
    else if (name == "HasRareResource") { result.has_rare_resource = boolean(member, p); rare = true; }
    else if (name == "HasAnomaly") { result.has_anomaly = boolean(member, p); anomaly = true; }
    else if (name == "HasPreWarpCivilization") { result.has_pre_warp_civilization = boolean(member, p); prewarp = true; }
    else if (name == "OrbitalEccentricity") result.orbital_eccentricity = number(member, p);
    else if (name == "OrbitalInclinationDegrees") result.orbital_inclination_degrees = number(member, p);
  }
  if (!(id && system && parent && orbit && name_present && kind && radius && mass && environment && legacy && rare && anomaly && prewarp))
    json_error(GalaxyPayloadJsonErrorPhase::RequiredMember, value, path,
               "JSON deserialization was missing required planetary body properties.");
  return result;
}

CivilizationCharacter decode_character(const Value &value,
                                         const std::string &path) {
  CivilizationCharacter result;
  bool id{}, display_name{};
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") { result.id = string(member, p); id = true; }
    else if (name == "DisplayName") { result.display_name = string(member, p); display_name = true; }
    else if (name == "VoiceProfileId") result.voice_profile_id = optional_value<std::string>(member, p, string);
    else if (name == "Portrait") result.portrait = optional_value<std::string>(member, p, string);
  }
  if (!id)
    json_error(GalaxyPayloadJsonErrorPhase::Representability, value,
               child(path, "Id"),
               "System.Text.Json produced a null character id that the native DTO cannot retain.");
  if (!display_name)
    json_error(GalaxyPayloadJsonErrorPhase::Representability, value,
               child(path, "DisplayName"),
               "System.Text.Json produced a null character display name that the native DTO cannot retain.");
  return result;
}

CivilizationPersistenceDto decode_civilization(const Value &value,
                                                const std::string &path) {
  CivilizationPersistenceDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Leadership") {
      result.leadership.clear();
      result.leadership_present = !is_null(member);
      if (result.leadership_present) {
        for (const auto &[office, character] : object(member, p)) {
          const auto existing = std::ranges::find(result.leadership, office,
              &CivilizationPersistenceDto::LeadershipEntry::office);
          auto decoded = optional_value<CivilizationCharacter>(
              character, child(p, office), decode_character);
          if (existing == result.leadership.end())
            result.leadership.push_back({office, std::move(decoded)});
          else
            existing->character = std::move(decoded);
        }
      }
    } else if (name == "Id") result.id = integer<int>(member, p);
    else if (name == "Name") result.name = string(member, p);
    else if (name == "HomeSystemId") result.home_system_id = integer<int>(member, p);
    else if (name == "Archetype") result.archetype = enumeration<CivilizationArchetype>(member, p);
    else if (name == "Aggression") result.traits.aggression = number(member, p);
    else if (name == "Territoriality") result.traits.territoriality = number(member, p);
    else if (name == "Greed") result.traits.greed = number(member, p);
    else if (name == "ScientificCuriosity") result.traits.scientific_curiosity = number(member, p);
    else if (name == "RiskTolerance") result.traits.risk_tolerance = number(member, p);
    else if (name == "SurvivalPriority") result.traits.survival_priority = number(member, p);
    else if (name == "HonorBound") result.traits.honor_bound = boolean(member, p);
    else if (name == "IsPlayer") result.is_player = boolean(member, p);
    else if (name == "DevelopmentStage") result.development_stage = enumeration<CivilizationDevelopmentStage>(member, p);
    else if (name == "IsSeededAncient") result.is_seeded_ancient = boolean(member, p);
    else if (name == "ExpansionAllowed") result.expansion_allowed = boolean(member, p);
    else if (name == "NeutralUnlessProvoked") result.neutral_unless_provoked = boolean(member, p);
    else if (name == "SpeciesId") result.species_id = string(member, p);
  }
  return result;
}

SurfaceBuilding decode_surface_building(const Value &value,
                                        const std::string &path) {
  SurfaceBuilding result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = integer<int>(member, p);
    else if (name == "TypeId") result.type_id = string(member, p);
    else if (name == "X") result.x = single(member, p);
    else if (name == "Z") result.z = single(member, p);
    else if (name == "RotationDegrees") result.rotation_degrees = single(member, p);
    else if (name == "IndustryProgress") result.industry_progress = number(member, p);
    else if (name == "IsComplete") result.is_complete = boolean(member, p);
    else if (name == "IsEnabled") result.is_enabled = boolean(member, p);
    else if (name == "PendingUpgradeTypeId") result.pending_upgrade_type_id = optional_value<std::string>(member, p, string);
    else if (name == "UpgradeDaysRemaining") result.upgrade_days_remaining = number(member, p);
    else if (name == "OperatingPriority") result.operating_priority = integer<int>(member, p);
    else if (name == "Condition") result.condition = number(member, p);
    else if (name == "StoredPowerDays") result.stored_power_days = number(member, p);
  }
  return result;
}

ColonySaveDto decode_colony(const Value &value, const std::string &path) {
  ColonySaveDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = integer<int>(member, p);
    else if (name == "CivilizationId") result.civilization_id = integer<int>(member, p);
    else if (name == "SystemId") result.system_id = integer<int>(member, p);
    else if (name == "PlanetaryBodyId") result.planetary_body_id = optional_value<int>(member, p, integer<int>);
    else if (name == "Name") result.name = string(member, p);
    else if (name == "Kind") result.kind = enumeration<SettlementKind>(member, p);
    else if (name == "PopulationSpeciesId") result.population_species_id = optional_value<std::string>(member, p, string);
    else if (name == "PopulationMillions") result.population_millions = number(member, p);
    else if (name == "Infrastructure") result.infrastructure = number(member, p);
    else if (name == "Stability") result.stability = number(member, p);
    else if (name == "StoredFoodPopulationDaysMillions") result.stored_food_population_days_millions = number(member, p);
    else if (name == "StoredWaterPopulationDaysMillions") result.stored_water_population_days_millions = number(member, p);
    else if (name == "StoredExtractedMaterials") result.stored_extracted_materials = number(member, p);
    else if (name == "RemainingExtractableMaterials") result.remaining_extractable_materials = optional_value<double>(member, p, number);
    else if (name == "SurfaceHubLevel") result.surface_hub_level = optional_value<int>(member, p, integer<int>);
    else if (name == "SurfaceHubUpgradeDaysRemaining") result.surface_hub_upgrade_days_remaining = number(member, p);
    else if (name == "SurfaceBuildings") result.surface_buildings = optional_list<SurfaceBuilding>(member, p, decode_surface_building);
  }
  return result;
}

EconomySaveDto decode_economy(const Value &value, const std::string &path) {
  EconomySaveDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "CivilizationId") result.civilization_id = integer<int>(member, p);
    else if (name == "Credits") result.credits = number(member, p);
    else if (name == "Industry") result.industry = number(member, p);
    else if (name == "Science") result.science = number(member, p);
    else if (name == "LastCreditsPerSecond") result.last_credits_per_second = number(member, p);
    else if (name == "LastIndustryPerSecond") result.last_industry_per_second = number(member, p);
    else if (name == "LastSciencePerSecond") result.last_science_per_second = number(member, p);
    else if (name == "LastResearchSpendingPerDay") result.last_research_spending_per_day = number(member, p);
    else if (name == "LastResearchFundingFraction") result.last_research_funding_fraction = number(member, p);
    else if (name == "OperatingArrears") result.operating_arrears = number(member, p);
    else if (name == "LastBaseOperationsFundingFraction") result.last_base_operations_funding_fraction = number(member, p);
    else if (name == "IndustryPriority") result.industry_priority = optional_value<IndustryPriority>(member, p, enumeration<IndustryPriority>);
  }
  return result;
}

std::vector<std::string> string_list(const Value &value,
                                     const std::string &path) {
  return list<std::string>(value, path, string);
}

TechnologySaveDto decode_technology(const Value &value,
                                    const std::string &path) {
  TechnologySaveDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "CivilizationId") result.civilization_id = integer<int>(member, p);
    else if (name == "CompletedTechnologyIds") result.completed_technology_ids = optional_value<std::vector<std::string>>(member, p, string_list);
    else if (name == "ActiveResearchId") result.active_research_id = optional_value<std::string>(member, p, string);
    else if (name == "ActiveResearchProgress") result.active_research_progress = number(member, p);
  }
  if (!result.completed_technology_ids)
    result.completed_technology_ids = std::vector<std::string>{};
  return result;
}

QueuedConstructionProjectSaveDto decode_queued_project(
    const Value &value, const std::string &path) {
  QueuedConstructionProjectSaveDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "ProjectId") result.project_id = string(member, p);
    else if (name == "AuthorizationCredits") result.authorization_credits = number(member, p);
  }
  return result;
}

ConstructionSaveDto decode_construction(const Value &value,
                                        const std::string &path) {
  ConstructionSaveDto result;
  result.completed_project_ids = std::vector<std::string>{};
  result.queued_projects = std::vector<std::optional<QueuedConstructionProjectSaveDto>>{};
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "CivilizationId") result.civilization_id = integer<int>(member, p);
    else if (name == "CompletedProjectIds") result.completed_project_ids = optional_value<std::vector<std::string>>(member, p, string_list);
    else if (name == "ActiveProjectId") result.active_project_id = optional_value<std::string>(member, p, string);
    else if (name == "ActiveProjectProgress") result.active_project_progress = number(member, p);
    else if (name == "ActiveProjectAuthorizationCredits") result.active_project_authorization_credits = number(member, p);
    else if (name == "QueuedProjects") {
      if (is_null(member)) result.queued_projects = std::nullopt;
      else {
        std::vector<std::optional<QueuedConstructionProjectSaveDto>> projects;
        const auto &items = array(member, p);
        for (std::size_t index = 0; index != items.size(); ++index)
          projects.push_back(optional_value<QueuedConstructionProjectSaveDto>(
              items[index], p + '[' + std::to_string(index) + ']', decode_queued_project));
        result.queued_projects = std::move(projects);
      }
    }
  }
  return result;
}

SystemSurveyPersistenceDto decode_survey(const Value &value,
                                         const std::string &path) {
  SystemSurveyPersistenceDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "SystemId") result.system_id = integer<int>(member, p);
    else if (name == "Level") result.level = enumeration<SystemSurveyLevel>(member, p);
    else if (name == "Progress") result.progress = number(member, p);
  }
  return result;
}

std::vector<int> int_list(const Value &value, const std::string &path) {
  return list<int>(value, path, integer<int>);
}

CivilizationKnowledgePersistenceDto decode_knowledge_entry(
    const Value &value, const std::string &path) {
  CivilizationKnowledgePersistenceDto result;
  result.known_system_ids = std::vector<int>{};
  result.known_civilization_ids = std::vector<int>{};
  result.system_surveys =
      std::vector<std::optional<SystemSurveyPersistenceDto>>{};
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "CivilizationId") result.civilization_id = integer<int>(member, p);
    else if (name == "GalacticCoreAccessUnlocked") result.galactic_core_access_unlocked = boolean(member, p);
    else if (name == "GalacticCoreExplored") result.galactic_core_explored = boolean(member, p);
    else if (name == "KnownSystemIds") result.known_system_ids = optional_value<std::vector<int>>(member, p, int_list);
    else if (name == "KnownCivilizationIds") result.known_civilization_ids = optional_value<std::vector<int>>(member, p, int_list);
    else if (name == "SystemSurveys") {
      if (is_null(member)) result.system_surveys = std::nullopt;
      else {
        std::vector<std::optional<SystemSurveyPersistenceDto>> surveys;
        const auto &items = array(member, p);
        for (std::size_t index = 0; index != items.size(); ++index)
          surveys.push_back(optional_value<SystemSurveyPersistenceDto>(
              items[index], p + '[' + std::to_string(index) + ']', decode_survey));
        result.system_surveys = std::move(surveys);
      }
    }
  }
  return result;
}

QueuedShipBuildPersistenceDto decode_queued_ship(const Value &value,
                                                 const std::string &path) {
  QueuedShipBuildPersistenceDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "OrderId") result.order_id = optional_value<std::string>(member, p, string);
    else if (name == "DesignId") result.design_id = string(member, p);
    else if (name == "AuthorizationCredits") result.authorization_credits = number(member, p);
    else if (name == "ReservedPopulationMillions") result.reserved_population_millions = number(member, p);
    else if (name == "ReservedPopulationSpeciesId") result.reserved_population_species_id = optional_value<std::string>(member, p, string);
    else if (name == "ReservedPopulationSourceColonyId") result.reserved_population_source_colony_id = optional_value<int>(member, p, integer<int>);
  }
  return result;
}

ShipyardPersistenceDto decode_shipyard(const Value &value,
                                       const std::string &path) {
  ShipyardPersistenceDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "CivilizationId") result.civilization_id = integer<int>(member, p);
    else if (name == "NextOrderSequence") result.next_order_sequence = integer<std::int64_t>(member, p);
    else if (name == "ActiveDesignId") result.active_design_id = optional_value<std::string>(member, p, string);
    else if (name == "ActiveOrderId") result.active_order_id = optional_value<std::string>(member, p, string);
    else if (name == "ActiveBuildProgress") result.active_build_progress = number(member, p);
    else if (name == "ActiveAuthorizationCredits") result.active_authorization_credits = number(member, p);
    else if (name == "ReservedPopulationMillions") result.reserved_population_millions = number(member, p);
    else if (name == "ReservedPopulationSpeciesId") result.reserved_population_species_id = optional_value<std::string>(member, p, string);
    else if (name == "ReservedPopulationSourceColonyId") result.reserved_population_source_colony_id = optional_value<int>(member, p, integer<int>);
    else if (name == "QueuedBuilds") {
      result.queued_builds.clear();
      result.queued_builds_present = !is_null(member);
      if (result.queued_builds_present)
        result.queued_builds = list<QueuedShipBuildPersistenceDto>(member, p, decode_queued_ship);
    }
  }
  return result;
}

MassivePoint decode_point(const Value &value, const std::string &path) {
  MassivePoint result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "X") result.x = single(member, p);
    else if (name == "Y") result.y = single(member, p);
  }
  return result;
}

MassiveWeaponGroup decode_weapon(const Value &value,
                                 const std::string &path) {
  MassiveWeaponGroup result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = string(member, p);
    else if (name == "Kind") result.kind = enumeration<MassiveWeaponKind>(member, p);
    else if (name == "MountsPerShip") result.mounts_per_ship = integer<int>(member, p);
    else if (name == "DamagePerShot") result.damage_per_shot = single(member, p);
    else if (name == "ShotsPerSecond") result.shots_per_second = single(member, p);
    else if (name == "Range") result.range = single(member, p);
    else if (name == "Accuracy") result.accuracy = single(member, p);
    else if (name == "PowerPerSecond") result.power_per_second = single(member, p);
    else if (name == "HeatPerSecond") result.heat_per_second = single(member, p);
  }
  return result;
}

MassiveModuleState decode_module(const Value &value,
                                 const std::string &path) {
  MassiveModuleState result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = string(member, p);
    else if (name == "Kind") result.kind = enumeration<MassiveModuleKind>(member, p);
    else if (name == "InstalledCount") result.installed_count = integer<int>(member, p);
    else if (name == "MassEach") result.mass_each = single(member, p);
    else if (name == "PowerPerSecondEach") result.power_per_second_each = single(member, p);
    else if (name == "HeatPerSecondEach") result.heat_per_second_each = single(member, p);
    else if (name == "Condition") result.condition = single(member, p);
    else if (name == "Enabled") result.enabled = boolean(member, p);
    else if (name == "EffectiveRange") result.effective_range = single(member, p);
    else if (name == "FieldStrength") result.field_strength = single(member, p);
    else if (name == "DetectionSignature") result.detection_signature = single(member, p);
    else if (name == "Slots") result.slots = integer<int>(member, p);
  }
  return result;
}

MassiveCombatLoadout decode_loadout(const Value &value,
                                    const std::string &path) {
  MassiveCombatLoadout result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "MassPerShip") result.mass_per_ship = single(member, p);
    else if (name == "Acceleration") result.acceleration = single(member, p);
    else if (name == "MaximumSpeed") result.maximum_speed = single(member, p);
    else if (name == "ShieldPerShip") result.shield_per_ship = single(member, p);
    else if (name == "ArmorPerShip") result.armor_per_ship = single(member, p);
    else if (name == "HullPerShip") result.hull_per_ship = single(member, p);
    else if (name == "ReactorOutputPerShip") result.reactor_output_per_ship = single(member, p);
    else if (name == "CoolingPerShip") result.cooling_per_ship = single(member, p);
    else if (name == "WarpStabilization") result.warp_stabilization = single(member, p);
    else if (name == "WarpSpoolSeconds") result.warp_spool_seconds = single(member, p);
    else if (name == "ModuleSlotCapacity") result.module_slot_capacity = integer<int>(member, p);
    else if (name == "MaximumModuleMass") result.maximum_module_mass = single(member, p);
    else if (name == "Weapons") {
      if (is_null(member))
        json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p,
                   "System.Text.Json accepted a null tactical weapon list that the native DTO cannot retain.");
      result.weapons = list<MassiveWeaponGroup>(member, p, decode_weapon);
    } else if (name == "Modules") {
      if (is_null(member))
        json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p,
                   "System.Text.Json accepted a null tactical module list that the native DTO cannot retain.");
      result.modules = list<MassiveModuleState>(member, p, decode_module);
    }
  }
  return result;
}

MassiveVesselState decode_vessel(const Value &value,
                                 const std::string &path) {
  MassiveVesselState result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = integer<std::int64_t>(member, p);
    else if (name == "Name") result.name = string(member, p);
    else if (name == "DesignId") result.design_id = string(member, p);
    else if (name == "IsFlagship") result.is_flagship = boolean(member, p);
    else if (name == "IsCarrier") result.is_carrier = boolean(member, p);
    else if (name == "IsInterdictor") result.is_interdictor = boolean(member, p);
    else if (name == "IsStoryShip") result.is_story_ship = boolean(member, p);
    else if (name == "HullFraction") result.hull_fraction = single(member, p);
    else if (name == "EngineFraction") result.engine_fraction = single(member, p);
    else if (name == "SensorFraction") result.sensor_fraction = single(member, p);
    else if (name == "WarpDriveFraction") result.warp_drive_fraction = single(member, p);
    else if (name == "ReactorFraction") result.reactor_fraction = single(member, p);
    else if (name == "InterdictorFraction") result.interdictor_fraction = single(member, p);
    else if (name == "BattlesFought") result.battles_fought = integer<int>(member, p);
    else if (name == "ConfirmedKills") result.confirmed_kills = integer<int>(member, p);
    else if (name == "Destroyed") result.destroyed = boolean(member, p);
    else if (name == "Escaped") result.escaped = boolean(member, p);
  }
  return result;
}

FleetCombatSaveDto decode_fleet_combat(const Value &value,
                                       const std::string &path) {
  FleetCombatSaveDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "ProfileId") result.profile_id = string(member, p);
    else if (name == "Shields") result.shields = number(member, p);
    else if (name == "Armor") result.armor = number(member, p);
    else if (name == "Hull") result.hull = number(member, p);
    else if (name == "WeaponCooldownRemainingDays") result.weapon_cooldown_remaining_days = number(member, p);
    else if (name == "Order") result.order = enumeration<MilitaryOrderType>(member, p);
    else if (name == "TargetFleetId") result.target_fleet_id = optional_value<int>(member, p, integer<int>);
    else if (name == "DefendSystemId") result.defend_system_id = optional_value<int>(member, p, integer<int>);
    else if (name == "RetreatProgressDays") result.retreat_progress_days = number(member, p);
    else if (name == "RetreatStarted") result.retreat_started = boolean(member, p);
    else if (name == "IsDisengaged") result.is_disengaged = boolean(member, p);
    else if (name == "DisengagedSystemId") result.disengaged_system_id = optional_value<int>(member, p, integer<int>);
  }
  return result;
}

FleetSaveDto decode_fleet(const Value &value, const std::string &path) {
  FleetSaveDto result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = integer<int>(member, p);
    else if (name == "CivilizationId") result.civilization_id = integer<int>(member, p);
    else if (name == "Name") result.name = string(member, p);
    else if (name == "Role") result.role = enumeration<FleetRole>(member, p);
    else if (name == "DesignId") result.design_id = optional_value<std::string>(member, p, string);
    else if (name == "X") result.x = single(member, p);
    else if (name == "Y") result.y = single(member, p);
    else if (name == "CurrentSystemId") result.current_system_id = optional_value<int>(member, p, integer<int>);
    else if (name == "DestinationSystemId") result.destination_system_id = optional_value<int>(member, p, integer<int>);
    else if (name == "TransitPhase") result.transit_phase = enumeration<FleetTransitPhase>(member, p);
    else if (name == "TransitOriginSystemId") result.transit_origin_system_id = optional_value<int>(member, p, integer<int>);
    else if (name == "TransitTargetSystemId") result.transit_target_system_id = optional_value<int>(member, p, integer<int>);
    else if (name == "TransitProgress") result.transit_progress = number(member, p);
    else if (name == "LocalTransitStartX") result.local_transit_start_x = single(member, p);
    else if (name == "LocalTransitStartY") result.local_transit_start_y = single(member, p);
    else if (name == "LocalTransitPositionX") result.local_transit_position_x = single(member, p);
    else if (name == "LocalTransitPositionY") result.local_transit_position_y = single(member, p);
    else if (name == "LocalTransitTargetX") result.local_transit_target_x = single(member, p);
    else if (name == "LocalTransitTargetY") result.local_transit_target_y = single(member, p);
    else if (name == "PlannedRouteSystemIds") result.planned_route_system_ids = optional_value<std::vector<int>>(member, p, int_list);
    else if (name == "HoldRequested") result.hold_requested = boolean(member, p);
    else if (name == "ReturnToBaseRequested") result.return_to_base_requested = boolean(member, p);
    else if (name == "ReturnToBaseFailureReason") result.return_to_base_failure_reason = optional_value<std::string>(member, p, string);
    else if (name == "MissionOrderRevision") result.mission_order_revision = integer<int>(member, p);
    else if (name == "DestinationPlanetaryBodyId") result.destination_planetary_body_id = optional_value<int>(member, p, integer<int>);
    else if (name == "PreventAutomaticSettlement") result.prevent_automatic_settlement = boolean(member, p);
    else if (name == "SettlementBodyId") result.settlement_body_id = optional_value<int>(member, p, integer<int>);
    else if (name == "SettlementDaysCompleted") result.settlement_days_completed = number(member, p);
    else if (name == "ReconnaissanceSystemId") result.reconnaissance_system_id = optional_value<int>(member, p, integer<int>);
    else if (name == "ReconnaissanceDaysCompleted") result.reconnaissance_days_completed = number(member, p);
    else if (name == "FreightTargetOutpostId") result.freight_target_outpost_id = optional_value<int>(member, p, integer<int>);
    else if (name == "FreightHomeColonyId") result.freight_home_colony_id = optional_value<int>(member, p, integer<int>);
    else if (name == "CargoMaterialCapacity") result.cargo_material_capacity = number(member, p);
    else if (name == "CargoMaterials") result.cargo_materials = number(member, p);
    else if (name == "StrategicSpeed") result.strategic_speed = number(member, p);
    else if (name == "MaximumLegRangeLightYears") result.maximum_leg_range_light_years = number(member, p);
    else if (name == "FuelCapacityLightYears") result.fuel_capacity_light_years = number(member, p);
    else if (name == "FuelRemainingLightYears") result.fuel_remaining_light_years = optional_value<double>(member, p, number);
    else if (name == "SensorRange") result.sensor_range = single(member, p);
    else if (name == "IsActive") result.is_active = boolean(member, p);
    else if (name == "EmbarkedPopulationMillions") result.embarked_population_millions = optional_value<double>(member, p, number);
    else if (name == "EmbarkedPopulationSpeciesId") result.embarked_population_species_id = optional_value<std::string>(member, p, string);
    else if (name == "Combat") result.combat = optional_value<FleetCombatSaveDto>(member, p, decode_fleet_combat);
    else if (name == "TacticalLoadout") result.tactical_loadout = optional_value<MassiveCombatLoadout>(member, p, decode_loadout);
    else if (name == "TacticalVessel") result.tactical_vessel = optional_value<MassiveVesselState>(member, p, decode_vessel);
  }
  return result;
}

std::array<std::uint8_t, 16> decode_guid(const Value &value,
                                         const std::string &path) {
  const auto text = string(value, path);
  if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
      text[18] != '-' || text[23] != '-')
    json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
               "The JSON value is not in a supported Guid format.");
  std::array<std::uint8_t, 16> canonical{};
  std::size_t digit{};
  auto nibble = [&](char character) -> unsigned {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    json_error(GalaxyPayloadJsonErrorPhase::Parse, value, path,
               "The JSON value is not in a supported Guid format.");
  };
  for (std::size_t index = 0; index != text.size();) {
    if (text[index] == '-') { ++index; continue; }
    const auto high = nibble(text[index++]);
    const auto low = nibble(text[index++]);
    canonical[digit++] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return {canonical[3], canonical[2], canonical[1], canonical[0],
          canonical[5], canonical[4], canonical[7], canonical[6],
          canonical[8], canonical[9], canonical[10], canonical[11],
          canonical[12], canonical[13], canonical[14], canonical[15]};
}

MassiveCohortState decode_cohort(const Value &value,
                                 const std::string &path) {
  MassiveCohortState result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = integer<std::int64_t>(member, p);
    else if (name == "DesignId") result.design_id = string(member, p);
    else if (name == "InitialCount") result.initial_count = integer<int>(member, p);
    else if (name == "ActiveCount") result.active_count = integer<int>(member, p);
    else if (name == "Experience") result.experience = single(member, p);
  }
  return result;
}

MassiveFormationState decode_formation(const Value &value,
                                       const std::string &path) {
  MassiveFormationState result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = integer<std::int64_t>(member, p);
    else if (name == "CivilizationId") result.civilization_id = integer<int>(member, p);
    else if (name == "FleetId") result.fleet_id = integer<int>(member, p);
    else if (name == "TaskForceId") result.task_force_id = integer<int>(member, p);
    else if (name == "Name") result.name = string(member, p);
    else if (name == "Position") result.position = decode_point(member, p);
    else if (name == "Velocity") result.velocity = decode_point(member, p);
    else if (name == "Heading") result.heading = decode_point(member, p);
    else if (name == "Objective") result.objective = decode_point(member, p);
    else if (name == "Shape") result.shape = enumeration<MassiveFormationShape>(member, p);
    else if (name == "Order") result.order = enumeration<MassiveCombatOrderType>(member, p);
    else if (name == "TargetFormationId") result.target_formation_id = optional_value<std::int64_t>(member, p, integer<std::int64_t>);
    else if (name == "ProtectedFormationId") result.protected_formation_id = optional_value<std::int64_t>(member, p, integer<std::int64_t>);
    else if (name == "InterdictorProtection") result.interdictor_protection = enumeration<InterdictorProtectionPolicy>(member, p);
    else if (name == "Cohesion") result.cohesion = single(member, p);
    else if (name == "Morale") result.morale = single(member, p);
    else if (name == "ShieldPool") result.shield_pool = single(member, p);
    else if (name == "ArmorPool") result.armor_pool = single(member, p);
    else if (name == "HullPool") result.hull_pool = single(member, p);
    else if (name == "HullLossThresholdPerShip") result.hull_loss_threshold_per_ship = single(member, p);
    else if (name == "Heat") result.heat = single(member, p);
    else if (name == "PowerReserve") result.power_reserve = single(member, p);
    else if (name == "WarpSpoolProgress") result.warp_spool_progress = single(member, p);
    else if (name == "WarpBlocked") result.warp_blocked = boolean(member, p);
    else if (name == "Escaped") result.escaped = boolean(member, p);
    else if (name == "Surrendered") result.surrendered = boolean(member, p);
    else if (name == "InitialShipCount") result.initial_ship_count = integer<int>(member, p);
    else if (name == "DestroyedShips") result.destroyed_ships = integer<int>(member, p);
    else if (name == "HullDamageRemainder") result.hull_damage_remainder = single(member, p);
    else if (name == "Loadout") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null formation loadout that the native DTO cannot retain.");
      result.loadout = decode_loadout(member, p);
    } else if (name == "Cohorts") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null cohort list that the native DTO cannot retain.");
      result.cohorts = list<MassiveCohortState>(member, p, decode_cohort);
    } else if (name == "ImportantVessels") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null important-vessel list that the native DTO cannot retain.");
      result.important_vessels = list<MassiveVesselState>(member, p, decode_vessel);
    }
  }
  return result;
}

MassiveCombatEvent decode_event(const Value &value,
                                const std::string &path) {
  MassiveCombatEvent result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Sequence") result.sequence = integer<std::int64_t>(member, p);
    else if (name == "Tick") result.tick = integer<std::int64_t>(member, p);
    else if (name == "Type") result.type = enumeration<MassiveCombatEventType>(member, p);
    else if (name == "ActorCivilizationId") result.actor_civilization_id = integer<int>(member, p);
    else if (name == "ActorFormationId") result.actor_formation_id = integer<std::int64_t>(member, p);
    else if (name == "TargetCivilizationId") result.target_civilization_id = optional_value<int>(member, p, integer<int>);
    else if (name == "TargetFormationId") result.target_formation_id = optional_value<std::int64_t>(member, p, integer<std::int64_t>);
    else if (name == "Magnitude") result.magnitude = integer<int>(member, p);
    else if (name == "Position") result.position = decode_point(member, p);
    else if (name == "Message") result.message = string(member, p);
  }
  return result;
}

MassiveMissileSalvoState decode_salvo(const Value &value,
                                      const std::string &path) {
  MassiveMissileSalvoState result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "Id") result.id = integer<std::int64_t>(member, p);
    else if (name == "SourceFormationId") result.source_formation_id = integer<std::int64_t>(member, p);
    else if (name == "TargetFormationId") result.target_formation_id = integer<std::int64_t>(member, p);
    else if (name == "MissileCount") result.missile_count = integer<int>(member, p);
    else if (name == "Damage") result.damage = single(member, p);
    else if (name == "RemainingSeconds") result.remaining_seconds = single(member, p);
    else if (name == "LaunchPosition") result.launch_position = optional_value<MassivePoint>(member, p, decode_point);
    else if (name == "InitialFlightSeconds") result.initial_flight_seconds = single(member, p);
  }
  return result;
}

MassiveCombatBattleState decode_battle(const Value &value,
                                       const std::string &path) {
  MassiveCombatBattleState result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "BattleId") result.battle_id = decode_guid(member, p);
    else if (name == "Seed") result.seed = integer<std::uint64_t>(member, p);
    else if (name == "Tick") result.tick = integer<std::int64_t>(member, p);
    else if (name == "SimulatedSeconds") result.simulated_seconds = number(member, p);
    else if (name == "PendingSeconds") result.pending_seconds = number(member, p);
    else if (name == "NextEventSequence") result.next_event_sequence = integer<std::int64_t>(member, p);
    else if (name == "NextSalvoId") result.next_salvo_id = integer<std::int64_t>(member, p);
    else if (name == "Formations") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null formation list that the native DTO cannot retain.");
      result.formations = list<MassiveFormationState>(member, p, decode_formation);
    } else if (name == "Events") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null event list that the native DTO cannot retain.");
      result.events = list<MassiveCombatEvent>(member, p, decode_event);
    } else if (name == "ActiveSalvos") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null salvo list that the native DTO cannot retain.");
      result.active_salvos = list<MassiveMissileSalvoState>(member, p, decode_salvo);
    }
  }
  return result;
}

CampaignCombatBinding decode_binding(const Value &value,
                                     const std::string &path) {
  CampaignCombatBinding result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "FleetId") result.fleet_id = integer<int>(member, p);
    else if (name == "FormationId") result.formation_id = integer<std::int64_t>(member, p);
  }
  return result;
}

CampaignCombatEngagement decode_engagement(const Value &value,
                                           const std::string &path) {
  CampaignCombatEngagement result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "FirstFormationId") result.first_formation_id = integer<std::int64_t>(member, p);
    else if (name == "SecondFormationId") result.second_formation_id = integer<std::int64_t>(member, p);
  }
  return result;
}

CampaignMassiveEncounter decode_encounter(const Value &value,
                                          const std::string &path) {
  CampaignMassiveEncounter result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "SystemId") result.system_id = integer<int>(member, p);
    else if (name == "StartedDay") result.started_day = number(member, p);
    else if (name == "Battle") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null battle that the native DTO cannot retain.");
      result.battle = decode_battle(member, p);
    } else if (name == "Vessels") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null encounter vessel list that the native DTO cannot retain.");
      result.vessels = list<CampaignCombatBinding>(member, p, decode_binding);
    } else if (name == "EngagedFormationPairs") {
      if (is_null(member)) json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p, "System.Text.Json accepted a null engagement list that the native DTO cannot retain.");
      result.engaged_formation_pairs = list<CampaignCombatEngagement>(member, p, decode_engagement);
    } else if (name == "LastObservedEventSequence") result.last_observed_event_sequence = integer<std::int64_t>(member, p);
    else if (name == "Reconciled") result.reconciled = boolean(member, p);
  }
  return result;
}

FleetPowerObservation decode_observation(const Value &value,
                                         const std::string &path) {
  FleetPowerObservation result;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    if (name == "ObserverId") result.observer_id = integer<int>(member, p);
    else if (name == "FleetId") result.fleet_id = integer<int>(member, p);
    else if (name == "Power") result.power = number(member, p);
    else if (name == "ObservedDay") result.observed_day = number(member, p);
    else if (name == "Evidence") result.evidence = string(member, p);
  }
  return result;
}

template <typename T, typename Decode>
void decode_optional_collection(std::optional<std::vector<T>> &target,
                                const Value &value, const std::string &path,
                                Decode decode) {
  if (is_null(value)) {
    target = std::nullopt;
    return;
  }
  target = list<T>(value, path, decode);
}

void decode_galaxy(GalaxyPayloadV16Dto &result, const Value &value,
                   const std::string &path) {
  result.systems = std::vector<StellarSystemPersistenceDto>{};
  result.planetary_bodies = {};
  result.civilizations = std::vector<CivilizationPersistenceDto>{};
  result.fleets = std::vector<FleetSaveDto>{};
  result.colonies = std::vector<ColonySaveDto>{};
  result.economies = std::vector<EconomySaveDto>{};
  result.technologies = std::vector<TechnologySaveDto>{};
  result.construction_states = std::vector<ConstructionSaveDto>{};
  result.shipyard_states = std::vector<ShipyardPersistenceDto>{};
  result.knowledge.entries_present = true;
  std::vector<std::pair<std::string, std::exception_ptr>> deferred;
  for (const auto &[name, member] : object(value, path)) {
    const auto p = child(path, name);
    std::erase_if(deferred, [&](const auto &entry) {
      return entry.first == name;
    });
    try {
    if (name == "Seed") result.seed = integer<std::int64_t>(member, p);
    else if (name == "GenerationMetadata") result.generation_metadata = decode_metadata(member, p);
    else if (name == "GalacticCore") result.galactic_core = decode_core(member, p);
    else if (name == "Systems") decode_optional_collection(result.systems, member, p, decode_system);
    else if (name == "PlanetaryBodies") {
      result.planetary_bodies.bodies.clear();
      result.planetary_bodies.bodies_present = !is_null(member);
      if (result.planetary_bodies.bodies_present) {
        const auto &items = array(member, p);
        for (std::size_t index = 0; index != items.size(); ++index)
          result.planetary_bodies.bodies.push_back(
              optional_value<PlanetaryBodyPersistenceDto>(
                  items[index], p + '[' + std::to_string(index) + ']',
                  decode_body));
      }
    } else if (name == "Civilizations") decode_optional_collection(result.civilizations, member, p, decode_civilization);
    else if (name == "Fleets") decode_optional_collection(result.fleets, member, p, decode_fleet);
    else if (name == "Colonies") decode_optional_collection(result.colonies, member, p, decode_colony);
    else if (name == "Economies") decode_optional_collection(result.economies, member, p, decode_economy);
    else if (name == "Technologies") decode_optional_collection(result.technologies, member, p, decode_technology);
    else if (name == "ConstructionStates") decode_optional_collection(result.construction_states, member, p, decode_construction);
    else if (name == "ShipyardStates") decode_optional_collection(result.shipyard_states, member, p, decode_shipyard);
    else if (name == "PlayerCivilizationId") result.player_civilization_id = integer<int>(member, p);
    else if (name == "Knowledge") {
      result.knowledge.entries.clear();
      result.knowledge.entries_present = !is_null(member);
      if (result.knowledge.entries_present) {
        const auto &items = array(member, p);
        for (std::size_t index = 0; index != items.size(); ++index)
          result.knowledge.entries.push_back(
              optional_value<CivilizationKnowledgePersistenceDto>(
                  items[index], p + '[' + std::to_string(index) + ']',
                  decode_knowledge_entry));
      }
    } else if (name == "ActiveCombatEncounter") {
      result.active_combat_encounter = optional_value<CampaignMassiveEncounter>(
          member, p, decode_encounter);
    } else if (name == "CombatIntelligence") {
      if (is_null(member))
        result.combat_intelligence = std::nullopt;
      else
        result.combat_intelligence =
            list<FleetPowerObservation>(member, p, decode_observation);
    }
    } catch (const GalaxyPayloadJsonError &error) {
      if (error.phase() != GalaxyPayloadJsonErrorPhase::Representability)
        throw;
      deferred.emplace_back(name, std::current_exception());
    }
  }
  if (!deferred.empty())
    std::rethrow_exception(deferred.front().second);
}

void replace_galaxy(GalaxyPayloadV16Dto &target,
                    GalaxyPayloadV16Dto replacement) {
  target.seed = replacement.seed;
  target.generation_metadata = std::move(replacement.generation_metadata);
  target.galactic_core = std::move(replacement.galactic_core);
  target.systems = std::move(replacement.systems);
  target.planetary_bodies = std::move(replacement.planetary_bodies);
  target.civilizations = std::move(replacement.civilizations);
  target.fleets = std::move(replacement.fleets);
  target.colonies = std::move(replacement.colonies);
  target.economies = std::move(replacement.economies);
  target.technologies = std::move(replacement.technologies);
  target.construction_states = std::move(replacement.construction_states);
  target.shipyard_states = std::move(replacement.shipyard_states);
  target.player_civilization_id = replacement.player_civilization_id;
  target.knowledge = std::move(replacement.knowledge);
  target.active_combat_encounter =
      std::move(replacement.active_combat_encounter);
  target.combat_intelligence = std::move(replacement.combat_intelligence);
}

GalaxyPayloadV16Dto decode_root(
    const Value &value,
    detail::GalaxyPayloadOrderedRootTransform transform = {}) {
  GalaxyPayloadV16Dto result;
  result.format_version = transform.format_version.value_or(0);
  result.game_version.clear();
  result.saved_at_utc = "0001-01-01T00:00:00+00:00";
  result.simulation_days = 0;
  bool galaxy_seen{};
  std::vector<std::pair<std::string, std::exception_ptr>> deferred;
  for (const auto &[name, member] : object(value, "$")) {
    if (std::ranges::find(transform.excluded_members, name) !=
        transform.excluded_members.end())
      continue;
    const auto p = child("$", name);
    std::erase_if(deferred, [&](const auto &entry) {
      return entry.first == name;
    });
    try {
    if (name == "FormatVersion") {
      if (!transform.format_version)
        result.format_version = integer<int>(member, p);
    }
    else if (name == "GameVersion") result.game_version = string(member, p);
    else if (name == "SavedAtUtc") result.saved_at_utc = canonical_datetime_offset(member, p);
    else if (name == "SimulationDays") result.simulation_days = number(member, p);
    else if (name == "SimulationSeconds") (void)number(member, p);
    else if (name == "Galaxy") {
      galaxy_seen = true;
      if (is_null(member))
        json_error(GalaxyPayloadJsonErrorPhase::Representability, member, p,
                   "System.Text.Json accepted a null Galaxy that the native DTO cannot retain.");
      GalaxyPayloadV16Dto replacement;
      decode_galaxy(replacement, member, p);
      replace_galaxy(result, std::move(replacement));
    } else if (name == "GalaxyFormatVersion") (void)integer<int>(member, p);
    else if (name == "Diplomacy" || name == "AdaptiveResearch") {
      if (!is_null(member)) {
        (void)object(member, p);
        json_error(
            GalaxyPayloadJsonErrorPhase::Representability, member, p,
            "System.Text.Json accepted a populated campaign subsystem that the Galaxy16 DTO does not retain.");
      }
    }
    } catch (const GalaxyPayloadJsonError &error) {
      if (error.phase() != GalaxyPayloadJsonErrorPhase::Representability)
        throw;
      deferred.emplace_back(name, std::current_exception());
    }
  }
  if (!deferred.empty())
    std::rethrow_exception(deferred.front().second);
  if (!galaxy_seen) {
    Value empty{Object{}, value.byte, value.line};
    decode_galaxy(result, empty, "$.Galaxy");
  }
  return result;
}

template <typename T>
Json optional_json(const std::optional<T> &value) {
  return value ? Json(*value) : Json(nullptr);
}

template <typename T, typename Encode>
Json encode_list(const std::vector<T> &values, Encode encode) {
  auto result = Json::array();
  for (const auto &value : values)
    result.push_back(encode(value));
  return result;
}

template <typename T, typename Encode>
Json encode_optional_list(const std::optional<std::vector<T>> &values,
                          Encode encode) {
  return values ? encode_list(*values, encode) : Json(nullptr);
}

Json encode_core(const GalacticCoreMetadata &value) {
  return {{"LandmarkKey", value.landmark_key},
          {"X", value.x},
          {"Y", value.y},
          {"ExclusionRadius", value.exclusion_radius}};
}

std::string ascii_lower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return character >= 'A' && character <= 'Z'
               ? static_cast<char>(character - 'A' + 'a')
               : static_cast<char>(character);
  });
  return value;
}

Json encode_metadata(const GalaxyGenerationMetadata &value) {
  Json result{{"EnteredSeed", value.entered_seed},
              {"InternalSeed", value.internal_seed},
              {"GeneratorVersion", value.generator_version},
              {"CreatedAtUtc", value.created_at_utc},
              {"SystemCount", value.system_count},
              {"GalaxyShape", value.galaxy_shape},
              {"StellarVariety", value.stellar_variety},
              {"PlanetBearingSystems", value.planet_bearing_systems},
              {"HabitableWorlds", value.habitable_worlds},
              {"GuaranteedNearbyHabitableWorlds",
               value.guaranteed_nearby_habitable_worlds},
              {"OtherCivilizations", value.other_civilizations},
              {"AncientCivilizations", value.ancient_civilizations},
              {"SpaceHazards", value.space_hazards},
              {"StartingDevelopment", value.starting_development},
              {"Difficulty", value.difficulty},
              {"ArtProfileVersion", value.art_profile_version},
              {"PlayerSpeciesId", optional_json(value.player_species_id)},
              {"AnomalyFrequency", value.anomaly_frequency},
              {"GalacticCore", value.galactic_core
                                    ? encode_core(*value.galactic_core)
                                    : Json(nullptr)}};
  result["SpoilerFreeSummary"] =
      std::to_string(value.system_count) + " systems · " +
      ascii_lower(value.stellar_variety) + " stellar variety · " +
      ascii_lower(value.habitable_worlds) + " habitable worlds · " +
      std::to_string(value.other_civilizations) +
      " other civilizations · " + ascii_lower(value.ancient_civilizations) +
      " ancient powers";
  return result;
}

Json encode_system(const StellarSystemPersistenceDto &value) {
  Json result{{"Id", value.id}, {"Name", value.name}, {"X", value.x},
              {"Y", value.y}};
  if (value.galactic_depth_light_years)
    result["GalacticDepthLightYears"] = *value.galactic_depth_light_years;
  if (value.stellar_catalog_id)
    result["StellarCatalogId"] = *value.stellar_catalog_id;
  result["Archetype"] = static_cast<int>(value.archetype);
  result["HasHabitableWorld"] = value.has_habitable_world;
  result["HasAnomaly"] = value.has_anomaly;
  result["HasRareResource"] = value.has_rare_resource;
  result["HasPreWarpCivilization"] = value.has_pre_warp_civilization;
  if (value.catalog_preset_id)
    result["CatalogPresetId"] = *value.catalog_preset_id;
  if (value.stellar_class)
    result["StellarClass"] = static_cast<int>(*value.stellar_class);
  if (value.secondary_stellar_class)
    result["SecondaryStellarClass"] =
        static_cast<int>(*value.secondary_stellar_class);
  if (value.tertiary_stellar_class)
    result["TertiaryStellarClass"] =
        static_cast<int>(*value.tertiary_stellar_class);
  return result;
}

Json encode_environment(const PlanetaryEnvironmentPersistenceDto &value) {
  return {{"GravityG", value.gravity_g},
          {"TemperatureKelvin", value.temperature_kelvin},
          {"PressureKPa", value.pressure_kpa},
          {"Atmosphere", static_cast<int>(value.atmosphere)},
          {"AvailableSolvent", static_cast<int>(value.available_solvent)},
          {"RadiationHazard", value.radiation_hazard},
          {"IsImmersedEnvironment", value.is_immersed_environment},
          {"HasSolidSurface", value.has_solid_surface}};
}

Json encode_body(const PlanetaryBodyPersistenceDto &value) {
  Json result{{"Id", value.id},
              {"SystemId", value.system_id},
              {"ParentBodyId", optional_json(value.parent_body_id)},
              {"OrbitIndex", value.orbit_index},
              {"Name", optional_json(value.name)},
              {"Kind", static_cast<int>(value.kind)},
              {"RadiusEarth", value.radius_earth},
              {"MassEarth", value.mass_earth},
              {"Environment", value.environment
                                  ? encode_environment(*value.environment)
                                  : Json(nullptr)},
              {"LegacyColonizationCandidate",
               value.legacy_colonization_candidate},
              {"HasRareResource", value.has_rare_resource},
              {"HasAnomaly", value.has_anomaly},
              {"HasPreWarpCivilization", value.has_pre_warp_civilization}};
  if (value.orbital_eccentricity != 0)
    result["OrbitalEccentricity"] = value.orbital_eccentricity;
  if (value.orbital_inclination_degrees != 0)
    result["OrbitalInclinationDegrees"] =
        value.orbital_inclination_degrees;
  return result;
}

Json encode_character(const CivilizationCharacter &value) {
  return {{"Id", value.id},
          {"DisplayName", value.display_name},
          {"VoiceProfileId", optional_json(value.voice_profile_id)},
          {"Portrait", optional_json(value.portrait)}};
}

Json encode_civilization(const CivilizationPersistenceDto &value) {
  Json leadership = nullptr;
  if (value.leadership_present) {
    leadership = Json::object();
    for (const auto &entry : value.leadership)
      leadership[entry.office] = entry.character
                                     ? encode_character(*entry.character)
                                     : Json(nullptr);
  }
  return {{"Leadership", std::move(leadership)},
          {"Id", value.id},
          {"Name", value.name},
          {"HomeSystemId", value.home_system_id},
          {"Archetype", static_cast<int>(value.archetype)},
          {"Aggression", value.traits.aggression},
          {"Territoriality", value.traits.territoriality},
          {"Greed", value.traits.greed},
          {"ScientificCuriosity", value.traits.scientific_curiosity},
          {"RiskTolerance", value.traits.risk_tolerance},
          {"SurvivalPriority", value.traits.survival_priority},
          {"HonorBound", value.traits.honor_bound},
          {"IsPlayer", value.is_player},
          {"DevelopmentStage", static_cast<int>(value.development_stage)},
          {"IsSeededAncient", value.is_seeded_ancient},
          {"ExpansionAllowed", value.expansion_allowed},
          {"NeutralUnlessProvoked", value.neutral_unless_provoked},
          {"SpeciesId", value.species_id}};
}

Json encode_surface_building(const SurfaceBuilding &value) {
  return {{"Id", value.id},
          {"TypeId", value.type_id},
          {"X", value.x},
          {"Z", value.z},
          {"RotationDegrees", value.rotation_degrees},
          {"IndustryProgress", value.industry_progress},
          {"IsComplete", value.is_complete},
          {"IsEnabled", value.is_enabled},
          {"PendingUpgradeTypeId", optional_json(value.pending_upgrade_type_id)},
          {"UpgradeDaysRemaining", value.upgrade_days_remaining},
          {"OperatingPriority", value.operating_priority},
          {"Condition", value.condition},
          {"StoredPowerDays", value.stored_power_days}};
}

Json encode_colony(const ColonySaveDto &value) {
  return {{"Id", value.id},
          {"CivilizationId", value.civilization_id},
          {"SystemId", value.system_id},
          {"PlanetaryBodyId", optional_json(value.planetary_body_id)},
          {"Name", value.name},
          {"Kind", static_cast<int>(value.kind)},
          {"PopulationSpeciesId", optional_json(value.population_species_id)},
          {"PopulationMillions", value.population_millions},
          {"Infrastructure", value.infrastructure},
          {"Stability", value.stability},
          {"StoredFoodPopulationDaysMillions",
           value.stored_food_population_days_millions},
          {"StoredWaterPopulationDaysMillions",
           value.stored_water_population_days_millions},
          {"StoredExtractedMaterials", value.stored_extracted_materials},
          {"RemainingExtractableMaterials",
           optional_json(value.remaining_extractable_materials)},
          {"SurfaceHubLevel", optional_json(value.surface_hub_level)},
          {"SurfaceHubUpgradeDaysRemaining",
           value.surface_hub_upgrade_days_remaining},
          {"SurfaceBuildings",
           value.surface_buildings
               ? encode_list(*value.surface_buildings, encode_surface_building)
               : Json(nullptr)}};
}

Json encode_economy(const EconomySaveDto &value) {
  return {{"CivilizationId", value.civilization_id},
          {"Credits", value.credits},
          {"Industry", value.industry},
          {"Science", value.science},
          {"LastCreditsPerSecond", value.last_credits_per_second},
          {"LastIndustryPerSecond", value.last_industry_per_second},
          {"LastSciencePerSecond", value.last_science_per_second},
          {"LastResearchSpendingPerDay", value.last_research_spending_per_day},
          {"LastResearchFundingFraction", value.last_research_funding_fraction},
          {"OperatingArrears", value.operating_arrears},
          {"LastBaseOperationsFundingFraction",
           value.last_base_operations_funding_fraction},
          {"IndustryPriority", value.industry_priority
                                   ? Json(static_cast<int>(*value.industry_priority))
                                   : Json(nullptr)}};
}

Json encode_technology(const TechnologySaveDto &value) {
  return {{"CivilizationId", value.civilization_id},
          {"CompletedTechnologyIds", value.completed_technology_ids
                                         ? Json(*value.completed_technology_ids)
                                         : Json(nullptr)},
          {"ActiveResearchId", optional_json(value.active_research_id)},
          {"ActiveResearchProgress", value.active_research_progress}};
}

Json encode_queued_project(const QueuedConstructionProjectSaveDto &value) {
  return {{"ProjectId", value.project_id},
          {"AuthorizationCredits", value.authorization_credits}};
}

Json encode_construction(const ConstructionSaveDto &value) {
  Json queued = nullptr;
  if (value.queued_projects) {
    queued = Json::array();
    for (const auto &item : *value.queued_projects)
      queued.push_back(item ? encode_queued_project(*item) : Json(nullptr));
  }
  return {{"CivilizationId", value.civilization_id},
          {"CompletedProjectIds", value.completed_project_ids
                                      ? Json(*value.completed_project_ids)
                                      : Json(nullptr)},
          {"ActiveProjectId", optional_json(value.active_project_id)},
          {"ActiveProjectProgress", value.active_project_progress},
          {"ActiveProjectAuthorizationCredits",
           value.active_project_authorization_credits},
          {"QueuedProjects", std::move(queued)}};
}

Json encode_survey(const SystemSurveyPersistenceDto &value) {
  return {{"SystemId", value.system_id},
          {"Level", static_cast<int>(value.level)},
          {"Progress", value.progress}};
}

Json encode_knowledge(const CivilizationKnowledgePersistenceDto &value) {
  Json surveys = nullptr;
  if (value.system_surveys) {
    surveys = Json::array();
    for (const auto &item : *value.system_surveys)
      surveys.push_back(item ? encode_survey(*item) : Json(nullptr));
  }
  return {{"CivilizationId", value.civilization_id},
          {"GalacticCoreAccessUnlocked", value.galactic_core_access_unlocked},
          {"GalacticCoreExplored", value.galactic_core_explored},
          {"KnownSystemIds", value.known_system_ids
                                 ? Json(*value.known_system_ids)
                                 : Json(nullptr)},
          {"KnownCivilizationIds", value.known_civilization_ids
                                       ? Json(*value.known_civilization_ids)
                                       : Json(nullptr)},
          {"SystemSurveys", std::move(surveys)}};
}

Json encode_queued_ship(const QueuedShipBuildPersistenceDto &value) {
  return {{"OrderId", optional_json(value.order_id)},
          {"DesignId", value.design_id},
          {"AuthorizationCredits", value.authorization_credits},
          {"ReservedPopulationMillions", value.reserved_population_millions},
          {"ReservedPopulationSpeciesId",
           optional_json(value.reserved_population_species_id)},
          {"ReservedPopulationSourceColonyId",
           optional_json(value.reserved_population_source_colony_id)}};
}

Json encode_shipyard(const ShipyardPersistenceDto &value) {
  return {{"CivilizationId", value.civilization_id},
          {"NextOrderSequence", value.next_order_sequence},
          {"ActiveDesignId", optional_json(value.active_design_id)},
          {"ActiveOrderId", optional_json(value.active_order_id)},
          {"ActiveBuildProgress", value.active_build_progress},
          {"ActiveAuthorizationCredits", value.active_authorization_credits},
          {"ReservedPopulationMillions", value.reserved_population_millions},
          {"ReservedPopulationSpeciesId",
           optional_json(value.reserved_population_species_id)},
          {"ReservedPopulationSourceColonyId",
           optional_json(value.reserved_population_source_colony_id)},
          {"QueuedBuilds", value.queued_builds_present
                               ? encode_list(value.queued_builds,
                                             encode_queued_ship)
                               : Json(nullptr)}};
}

Json encode_point(const MassivePoint &value) {
  return {{"X", value.x}, {"Y", value.y}, {"Vector", Json::object()},
          {"IsFinite", value.is_finite()}};
}

Json encode_weapon(const MassiveWeaponGroup &value) {
  return {{"Id", value.id},
          {"Kind", static_cast<int>(value.kind)},
          {"MountsPerShip", value.mounts_per_ship},
          {"DamagePerShot", value.damage_per_shot},
          {"ShotsPerSecond", value.shots_per_second},
          {"Range", value.range},
          {"Accuracy", value.accuracy},
          {"PowerPerSecond", value.power_per_second},
          {"HeatPerSecond", value.heat_per_second}};
}

Json encode_module(const MassiveModuleState &value) {
  return {{"Id", value.id},
          {"Kind", static_cast<int>(value.kind)},
          {"InstalledCount", value.installed_count},
          {"MassEach", value.mass_each},
          {"PowerPerSecondEach", value.power_per_second_each},
          {"HeatPerSecondEach", value.heat_per_second_each},
          {"Condition", value.condition},
          {"Enabled", value.enabled},
          {"EffectiveRange", value.effective_range},
          {"FieldStrength", value.field_strength},
          {"DetectionSignature", value.detection_signature},
          {"Slots", value.slots}};
}

Json encode_loadout(const MassiveCombatLoadout &value) {
  return {{"MassPerShip", value.mass_per_ship},
          {"Acceleration", value.acceleration},
          {"MaximumSpeed", value.maximum_speed},
          {"ShieldPerShip", value.shield_per_ship},
          {"ArmorPerShip", value.armor_per_ship},
          {"HullPerShip", value.hull_per_ship},
          {"ReactorOutputPerShip", value.reactor_output_per_ship},
          {"CoolingPerShip", value.cooling_per_ship},
          {"WarpStabilization", value.warp_stabilization},
          {"WarpSpoolSeconds", value.warp_spool_seconds},
          {"ModuleSlotCapacity", value.module_slot_capacity},
          {"MaximumModuleMass", value.maximum_module_mass},
          {"Weapons", encode_list(value.weapons, encode_weapon)},
          {"Modules", encode_list(value.modules, encode_module)}};
}

Json encode_vessel(const MassiveVesselState &value) {
  return {{"Id", value.id},
          {"Name", value.name},
          {"DesignId", value.design_id},
          {"IsFlagship", value.is_flagship},
          {"IsCarrier", value.is_carrier},
          {"IsInterdictor", value.is_interdictor},
          {"IsStoryShip", value.is_story_ship},
          {"HullFraction", value.hull_fraction},
          {"EngineFraction", value.engine_fraction},
          {"SensorFraction", value.sensor_fraction},
          {"WarpDriveFraction", value.warp_drive_fraction},
          {"ReactorFraction", value.reactor_fraction},
          {"InterdictorFraction", value.interdictor_fraction},
          {"BattlesFought", value.battles_fought},
          {"ConfirmedKills", value.confirmed_kills},
          {"Destroyed", value.destroyed},
          {"Escaped", value.escaped}};
}

Json encode_fleet_combat(const FleetCombatSaveDto &value) {
  return {{"ProfileId", value.profile_id},
          {"Shields", value.shields},
          {"Armor", value.armor},
          {"Hull", value.hull},
          {"WeaponCooldownRemainingDays",
           value.weapon_cooldown_remaining_days},
          {"Order", static_cast<int>(value.order)},
          {"TargetFleetId", optional_json(value.target_fleet_id)},
          {"DefendSystemId", optional_json(value.defend_system_id)},
          {"RetreatProgressDays", value.retreat_progress_days},
          {"RetreatStarted", value.retreat_started},
          {"IsDisengaged", value.is_disengaged},
          {"DisengagedSystemId", optional_json(value.disengaged_system_id)}};
}

Json encode_fleet(const FleetSaveDto &value) {
  Json result{{"Id", value.id},
              {"CivilizationId", value.civilization_id},
              {"Name", value.name},
              {"Role", static_cast<int>(value.role)},
              {"DesignId", optional_json(value.design_id)},
              {"X", value.x},
              {"Y", value.y},
              {"CurrentSystemId", optional_json(value.current_system_id)},
              {"DestinationSystemId",
               optional_json(value.destination_system_id)},
              {"TransitPhase", static_cast<int>(value.transit_phase)},
              {"TransitOriginSystemId",
               optional_json(value.transit_origin_system_id)},
              {"TransitTargetSystemId",
               optional_json(value.transit_target_system_id)},
              {"TransitProgress", value.transit_progress},
              {"LocalTransitStartX", value.local_transit_start_x},
              {"LocalTransitStartY", value.local_transit_start_y},
              {"LocalTransitPositionX", value.local_transit_position_x},
              {"LocalTransitPositionY", value.local_transit_position_y},
              {"LocalTransitTargetX", value.local_transit_target_x},
              {"LocalTransitTargetY", value.local_transit_target_y},
              {"PlannedRouteSystemIds",
               value.planned_route_system_ids
                   ? Json(*value.planned_route_system_ids)
                   : Json(nullptr)},
              {"HoldRequested", value.hold_requested},
              {"ReturnToBaseRequested", value.return_to_base_requested},
              {"ReturnToBaseFailureReason",
               optional_json(value.return_to_base_failure_reason)},
              {"MissionOrderRevision", value.mission_order_revision},
              {"DestinationPlanetaryBodyId",
               optional_json(value.destination_planetary_body_id)},
              {"PreventAutomaticSettlement",
               value.prevent_automatic_settlement},
              {"SettlementBodyId", optional_json(value.settlement_body_id)},
              {"SettlementDaysCompleted", value.settlement_days_completed},
              {"ReconnaissanceSystemId",
               optional_json(value.reconnaissance_system_id)},
              {"ReconnaissanceDaysCompleted",
               value.reconnaissance_days_completed},
              {"FreightTargetOutpostId",
               optional_json(value.freight_target_outpost_id)},
              {"FreightHomeColonyId",
               optional_json(value.freight_home_colony_id)},
              {"CargoMaterialCapacity", value.cargo_material_capacity},
              {"CargoMaterials", value.cargo_materials},
              {"StrategicSpeed", value.strategic_speed},
              {"MaximumLegRangeLightYears",
               value.maximum_leg_range_light_years},
              {"FuelCapacityLightYears", value.fuel_capacity_light_years},
              {"FuelRemainingLightYears",
               optional_json(value.fuel_remaining_light_years)},
              {"SensorRange", value.sensor_range},
              {"IsActive", value.is_active},
              {"EmbarkedPopulationMillions",
               optional_json(value.embarked_population_millions)},
              {"EmbarkedPopulationSpeciesId",
               optional_json(value.embarked_population_species_id)},
              {"Combat", value.combat ? encode_fleet_combat(*value.combat)
                                       : Json(nullptr)}};
  if (value.tactical_loadout)
    result["TacticalLoadout"] = encode_loadout(*value.tactical_loadout);
  if (value.tactical_vessel)
    result["TacticalVessel"] = encode_vessel(*value.tactical_vessel);
  return result;
}

std::string encode_guid(const std::array<std::uint8_t, 16> &bytes) {
  const std::array<std::uint8_t, 16> canonical = {
      bytes[3], bytes[2], bytes[1], bytes[0], bytes[5], bytes[4], bytes[7],
      bytes[6], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
      bytes[14], bytes[15]};
  std::ostringstream output;
  output << std::hex;
  for (std::size_t index = 0; index != canonical.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10)
      output << '-';
    output.width(2);
    output.fill('0');
    output << static_cast<unsigned>(canonical[index]);
  }
  return output.str();
}

Json encode_cohort(const MassiveCohortState &value) {
  return {{"Id", value.id},
          {"DesignId", value.design_id},
          {"InitialCount", value.initial_count},
          {"ActiveCount", value.active_count},
          {"Experience", value.experience}};
}

Json encode_formation(const MassiveFormationState &value) {
  return {{"Id", value.id},
          {"CivilizationId", value.civilization_id},
          {"FleetId", value.fleet_id},
          {"TaskForceId", value.task_force_id},
          {"Name", value.name},
          {"Position", encode_point(value.position)},
          {"Velocity", encode_point(value.velocity)},
          {"Heading", encode_point(value.heading)},
          {"Objective", encode_point(value.objective)},
          {"Shape", static_cast<int>(value.shape)},
          {"Order", static_cast<int>(value.order)},
          {"TargetFormationId", optional_json(value.target_formation_id)},
          {"ProtectedFormationId", optional_json(value.protected_formation_id)},
          {"InterdictorProtection",
           static_cast<int>(value.interdictor_protection)},
          {"Cohesion", value.cohesion},
          {"Morale", value.morale},
          {"ShieldPool", value.shield_pool},
          {"ArmorPool", value.armor_pool},
          {"HullPool", value.hull_pool},
          {"HullLossThresholdPerShip", value.hull_loss_threshold_per_ship},
          {"Heat", value.heat},
          {"PowerReserve", value.power_reserve},
          {"WarpSpoolProgress", value.warp_spool_progress},
          {"WarpBlocked", value.warp_blocked},
          {"Escaped", value.escaped},
          {"Surrendered", value.surrendered},
          {"InitialShipCount", value.initial_ship_count},
          {"DestroyedShips", value.destroyed_ships},
          {"HullDamageRemainder", value.hull_damage_remainder},
          {"Loadout", encode_loadout(value.loadout)},
          {"Cohorts", encode_list(value.cohorts, encode_cohort)},
          {"ImportantVessels",
           encode_list(value.important_vessels, encode_vessel)}};
}

Json encode_event(const MassiveCombatEvent &value) {
  return {{"Sequence", value.sequence},
          {"Tick", value.tick},
          {"Type", static_cast<int>(value.type)},
          {"ActorCivilizationId", value.actor_civilization_id},
          {"ActorFormationId", value.actor_formation_id},
          {"TargetCivilizationId",
           optional_json(value.target_civilization_id)},
          {"TargetFormationId", optional_json(value.target_formation_id)},
          {"Magnitude", value.magnitude},
          {"Position", encode_point(value.position)},
          {"Message", value.message}};
}

Json encode_salvo(const MassiveMissileSalvoState &value) {
  return {{"Id", value.id},
          {"SourceFormationId", value.source_formation_id},
          {"TargetFormationId", value.target_formation_id},
          {"MissileCount", value.missile_count},
          {"Damage", value.damage},
          {"RemainingSeconds", value.remaining_seconds},
          {"LaunchPosition", value.launch_position
                                 ? encode_point(*value.launch_position)
                                 : Json(nullptr)},
          {"InitialFlightSeconds", value.initial_flight_seconds}};
}

Json encode_battle(const MassiveCombatBattleState &value) {
  return {{"BattleId", encode_guid(value.battle_id)},
          {"Seed", value.seed},
          {"Tick", value.tick},
          {"SimulatedSeconds", value.simulated_seconds},
          {"PendingSeconds", value.pending_seconds},
          {"NextEventSequence", value.next_event_sequence},
          {"NextSalvoId", value.next_salvo_id},
          {"Formations", encode_list(value.formations, encode_formation)},
          {"Events", encode_list(value.events, encode_event)},
          {"ActiveSalvos", encode_list(value.active_salvos, encode_salvo)}};
}

Json encode_binding(const CampaignCombatBinding &value) {
  return {{"FleetId", value.fleet_id}, {"FormationId", value.formation_id}};
}

Json encode_engagement(const CampaignCombatEngagement &value) {
  return {{"FirstFormationId", value.first_formation_id},
          {"SecondFormationId", value.second_formation_id}};
}

Json encode_encounter(const CampaignMassiveEncounter &value) {
  return {{"SystemId", value.system_id},
          {"StartedDay", value.started_day},
          {"Battle", encode_battle(value.battle)},
          {"Vessels", encode_list(value.vessels, encode_binding)},
          {"EngagedFormationPairs",
           encode_list(value.engaged_formation_pairs, encode_engagement)},
          {"LastObservedEventSequence", value.last_observed_event_sequence},
          {"Reconciled", value.reconciled}};
}

Json encode_observation(const FleetPowerObservation &value) {
  return {{"ObserverId", value.observer_id},
          {"FleetId", value.fleet_id},
          {"Power", value.power},
          {"ObservedDay", value.observed_day},
          {"Evidence", value.evidence}};
}

std::string validate_encoded_datetime(std::string_view text,
                                      const std::string &path) {
  Value value{std::string(text), 0, 0};
  try {
    return canonical_datetime_offset(value, path);
  } catch (const GalaxyPayloadJsonError &error) {
    throw GalaxyPayloadJsonError(
        GalaxyPayloadJsonErrorPhase::Encode,
        "The native DateTimeOffset text cannot be encoded: " +
            std::string(error.what()),
        path);
  }
}

void reject_nonfinite(const Json &value, const std::string &path) {
  if (value.is_number_float() && !std::isfinite(value.get<double>()))
    throw GalaxyPayloadJsonError(
        GalaxyPayloadJsonErrorPhase::Encode,
        "System.Text.Json cannot write a non-finite floating-point value.",
        path);
  if (value.is_array()) {
    for (std::size_t index = 0; index != value.size(); ++index)
      reject_nonfinite(value[index], path + '[' + std::to_string(index) + ']');
  } else if (value.is_object()) {
    for (const auto &[name, member] : value.items())
      reject_nonfinite(member, child(path, name));
  }
}

Json encode_galaxy(const GalaxyPayloadV16Dto &value) {
  Json bodies = nullptr;
  if (value.planetary_bodies.bodies_present) {
    bodies = Json::array();
    for (const auto &item : value.planetary_bodies.bodies)
      bodies.push_back(item ? encode_body(*item) : Json(nullptr));
  }
  Json knowledge = nullptr;
  if (value.knowledge.entries_present) {
    knowledge = Json::array();
    for (const auto &item : value.knowledge.entries)
      knowledge.push_back(item ? encode_knowledge(*item) : Json(nullptr));
  }
  Json result{
      {"Seed", value.seed},
      {"GenerationMetadata", value.generation_metadata
                                 ? encode_metadata(*value.generation_metadata)
                                 : Json(nullptr)},
      {"GalacticCore", value.galactic_core
                           ? encode_core(*value.galactic_core)
                           : Json(nullptr)},
      {"Systems", encode_optional_list(value.systems, encode_system)},
      {"PlanetaryBodies", std::move(bodies)},
      {"Civilizations",
       encode_optional_list(value.civilizations, encode_civilization)},
      {"Fleets", encode_optional_list(value.fleets, encode_fleet)},
      {"Colonies", encode_optional_list(value.colonies, encode_colony)},
      {"Economies", encode_optional_list(value.economies, encode_economy)},
      {"Technologies",
       encode_optional_list(value.technologies, encode_technology)},
      {"ConstructionStates",
       encode_optional_list(value.construction_states, encode_construction)},
      {"ShipyardStates",
       encode_optional_list(value.shipyard_states, encode_shipyard)},
      {"PlayerCivilizationId", value.player_civilization_id},
      {"Knowledge", std::move(knowledge)},
  };
  if (value.active_combat_encounter)
    result["ActiveCombatEncounter"] =
        encode_encounter(*value.active_combat_encounter);
  if (value.combat_intelligence)
    result["CombatIntelligence"] =
        encode_list(*value.combat_intelligence, encode_observation);
  return result;
}

} // namespace

GalaxyPayloadV16Dto detail::decode_galaxy_payload_v16_ordered(
    const json_detail::Value &root,
    GalaxyPayloadOrderedRootTransform transform) {
  return decode_root(root, transform);
}

GalaxyPayloadJsonError::GalaxyPayloadJsonError(
    GalaxyPayloadJsonErrorPhase phase, std::string message, std::string path,
    std::optional<std::size_t> line, std::optional<std::size_t> byte)
    : std::runtime_error(std::move(message)), phase_(phase),
      path_(std::move(path)), line_(line), byte_(byte) {}

GalaxyPayloadJsonErrorPhase GalaxyPayloadJsonError::phase() const noexcept {
  return phase_;
}

const std::string &GalaxyPayloadJsonError::path() const noexcept {
  return path_;
}

const std::optional<std::size_t> &GalaxyPayloadJsonError::line() const noexcept {
  return line_;
}

const std::optional<std::size_t> &GalaxyPayloadJsonError::byte() const noexcept {
  return byte_;
}

GalaxyPayloadV16Dto
decode_galaxy_payload_v16_json(std::string_view utf8_json) {
  try {
    return decode_root(json_detail::parse_ordered_json(utf8_json));
  } catch (const json_detail::ParseFailure &error) {
    throw GalaxyPayloadJsonError(GalaxyPayloadJsonErrorPhase::Parse,
                                 error.what(), {}, error.line, error.byte);
  }
}

std::string
encode_galaxy_payload_v16_json(const GalaxyPayloadV16Dto &payload) {
  try {
    const auto saved_at =
        validate_encoded_datetime(payload.saved_at_utc, "$.SavedAtUtc");
    Json galaxy = encode_galaxy(payload);
    if (payload.generation_metadata) {
      galaxy["GenerationMetadata"]["CreatedAtUtc"] =
          validate_encoded_datetime(
              payload.generation_metadata->created_at_utc,
              "$.Galaxy.GenerationMetadata.CreatedAtUtc");
    }
    Json envelope{{"FormatVersion", payload.format_version},
                  {"GameVersion", payload.game_version},
                  {"SavedAtUtc", saved_at},
                  {"SimulationDays", payload.simulation_days},
                  {"SimulationSeconds", 0.0},
                  {"Galaxy", std::move(galaxy)}};
    reject_nonfinite(envelope, "$");
    return envelope.dump(2, ' ', true,
                         nlohmann::json::error_handler_t::strict);
  } catch (const nlohmann::json::exception &error) {
    throw GalaxyPayloadJsonError(
        GalaxyPayloadJsonErrorPhase::Encode,
        "The JSON encoder rejected the native payload: " +
            std::string(error.what()),
        "$");
  }
}

} // namespace stellar::core
