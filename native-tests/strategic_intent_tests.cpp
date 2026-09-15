#include <stellar/core/strategic_intent.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::core;

namespace {
std::vector<std::string> split(const std::string &text, char separator) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  do {
    const auto end = text.find(separator, begin);
    result.push_back(text.substr(begin, end - begin));
    if (end == std::string::npos) break;
    begin = end + 1;
  } while (true);
  return result;
}

double number(const std::string &text) {
  if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
  if (text == "Inf") return std::numeric_limits<double>::infinity();
  if (text == "-Inf") return -std::numeric_limits<double>::infinity();
  std::size_t consumed = 0;
  const double result = std::stod(text, &consumed);
  if (consumed != text.size()) throw std::runtime_error("invalid number");
  return result;
}

bool same_number(double left, double right) {
  if (std::isnan(left) && std::isnan(right)) return true;
  return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

unsigned hex_digit(char value) {
  if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
  if (value >= 'A' && value <= 'F') return static_cast<unsigned>(value - 'A' + 10);
  throw std::runtime_error("invalid uppercase hexadecimal fixture text");
}

std::string decode_hex(const std::string &text) {
  if (text.size() % 2 != 0) throw std::runtime_error("odd hexadecimal fixture text");
  std::string result;
  result.reserve(text.size() / 2);
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const unsigned byte = (hex_digit(text[i]) << 4U) | hex_digit(text[i + 1]);
    result.push_back(static_cast<char>(byte));
  }
  return result;
}

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

void apply_weights(CivilizationStrategicIntent &intent, const std::string &encoded) {
  for (const auto &item : split(encoded, ',')) {
    const auto fields = split(item, ':');
    require(fields.size() == 2, "weight fields");
    intent.weights[static_cast<StrategicPriorityType>(std::stoi(fields[0]))] = number(fields[1]);
  }
}

CivilizationStrategicIntent parse_intent(const std::string &encoded) {
  const auto fields = split(encoded, ';');
  require(fields.size() == 7, "intent payload fields");
  CivilizationStrategicIntent result;
  result.civilization_id = std::stoi(fields[0]);
  result.generated_at_tick = std::stoll(fields[1]);
  result.review_after_tick = std::stoll(fields[2]);
  apply_weights(result, fields[3]);
  const int role = std::stoi(fields[4]);
  if (role >= 0) result.preferred_new_fleet_role = static_cast<FleetRole>(role);
  result.defer_new_colonization = fields[5] == "1";
  result.summary = decode_hex(fields[6]);
  return result;
}

bool same_intent(const CivilizationStrategicIntent &a,
                 const CivilizationStrategicIntent &b) {
  if (a.civilization_id != b.civilization_id ||
      a.generated_at_tick != b.generated_at_tick ||
      a.review_after_tick != b.review_after_tick ||
      a.preferred_new_fleet_role != b.preferred_new_fleet_role ||
      a.defer_new_colonization != b.defer_new_colonization ||
      a.summary != b.summary || a.weights.size() != b.weights.size()) return false;
  for (const auto &[type, weight] : a.weights) {
    const auto found = b.weights.find(type);
    if (found == b.weights.end() || !same_number(weight, found->second)) return false;
  }
  return true;
}

void verify_provider_state(const std::vector<std::string> &fields,
                           StrategicIndustryPriorityProvider &industry,
                           StrategicShipbuildingPreferenceProvider &shipbuilding,
                           const std::string &tag) {
  require(industry.published_intent_count() == static_cast<std::size_t>(std::stoull(fields[7])), tag + ": industry count");
  require(shipbuilding.published_preference_count() == static_cast<std::size_t>(std::stoull(fields[8])), tag + ": shipbuilding count");
  for (const auto &encoded : split(fields[9], ',')) {
    const auto state = split(encoded, ':');
    require(state.size() == 5, tag + ": state fields");
    const int id = std::stoi(state[0]);
    const auto weights = industry.get_weights(id);
    const auto preference = shipbuilding.get_preference(id);
    require(same_number(weights.construction_weight, number(state[1])), tag + ": construction");
    require(same_number(weights.shipbuilding_weight, number(state[2])), tag + ": shipbuilding");
    const int role = preference.preferred_new_fleet_role ? static_cast<int>(*preference.preferred_new_fleet_role) : -1;
    require(preference.civilization_id == id, tag + ": preference id");
    require(role == std::stoi(state[3]), tag + ": role");
    require(preference.defer_new_colonization == (state[4] == "1"), tag + ": defer");
  }
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 2, "fixture path required");
  std::ifstream input(argv[1]);
  require(static_cast<bool>(input), "fixture unavailable");

  CivilizationStrategicIntentBuilder builder;
  StrategicIndustryPriorityProvider industry;
  StrategicShipbuildingPreferenceProvider shipbuilding;
  int intent_cases = 0, provider_operations = 0, formatter_cases = 0, source_only = 0;
  for (std::string line; std::getline(input, line);) {
    const auto fields = split(line, '\t');
    require(!fields.empty(), "empty fixture row");
    if (fields[0] == "I") {
      require(fields.size() == 11, "intent row fields");
      CivilizationStrategicPlan plan{std::stoi(fields[2]), std::stoll(fields[3]), std::stoll(fields[4])};
      if (!fields[5].empty()) for (const auto &encoded : split(fields[5], ',')) {
        const auto priority = split(encoded, ':');
        require(priority.size() == 3, fields[1] + ": priority fields");
        plan.priorities.push_back({static_cast<StrategicPriorityType>(std::stoi(priority[0])), number(priority[1]), decode_hex(priority[2])});
      }
      const auto input_copy = plan;
      const auto actual = builder.build(plan);
      require(plan.civilization_id == input_copy.civilization_id && plan.generated_at_tick == input_copy.generated_at_tick &&
              plan.review_after_tick == input_copy.review_after_tick && plan.priorities.size() == input_copy.priorities.size(), fields[1] + ": input metadata changed");
      require(actual.civilization_id == std::stoi(fields[2]) && actual.generated_at_tick == std::stoll(fields[3]) &&
              actual.review_after_tick == std::stoll(fields[4]), fields[1] + ": result metadata");
      for (const auto &encoded : split(fields[6], ',')) {
        const auto weight = split(encoded, ':');
        require(weight.size() == 2 && same_number(actual.get_weight(static_cast<StrategicPriorityType>(std::stoi(weight[0]))), number(weight[1])), fields[1] + ": weight");
      }
      const int role = actual.preferred_new_fleet_role ? static_cast<int>(*actual.preferred_new_fleet_role) : -1;
      require(role == std::stoi(fields[7]), fields[1] + ": preferred role");
      require(actual.defer_new_colonization == (fields[8] == "1"), fields[1] + ": defer");
      require(actual.summary == decode_hex(fields[9]), fields[1] + ": summary");
      require(fields[10] == "1", fields[1] + ": source mutated input");
      ++intent_cases;
    } else if (fields[0] == "F") {
      require(fields.size() == 3, "formatter row fields");
      CivilizationStrategicPlan plan{1, 2, 3, {{StrategicPriorityType::Explore, number(fields[1]), "fmt"}}};
      const auto actual = builder.build(plan);
      require(actual.summary == "Primary: Explore (" + decode_hex(fields[2]) + ") — fmt", "formatter parity " + fields[1]);
      ++formatter_cases;
    } else if (fields[0] == "P") {
      require(fields.size() == 10, "provider row fields");
      const std::string &tag = fields[1];
      if (fields[2] == "GET") {
        (void)industry.get_weights(std::stoi(fields[3]));
        (void)shipbuilding.get_preference(std::stoi(fields[3]));
      } else if (fields[2] == "PUBLISH_INTENT" || fields[2] == "PUBLISH_REVIEW") {
        auto intent = parse_intent(fields[4]);
        const auto before = intent;
        if (fields[2] == "PUBLISH_INTENT") {
          industry.publish(intent);
          shipbuilding.publish(intent);
        } else {
          CivilizationStrategicReview review;
          review.plan.civilization_id = std::stoi(fields[5]);
          review.intent = intent;
          industry.publish(review);
          shipbuilding.publish(review);
          require(same_intent(review.intent, intent), tag + ": review input changed");
          review.intent.weights.clear();
          review.intent.preferred_new_fleet_role.reset();
          review.intent.defer_new_colonization = !review.intent.defer_new_colonization;
        }
        require(same_intent(intent, before), tag + ": intent input changed");
        require(fields[6] == "1", tag + ": source mutated input");
        if (fields[2] == "PUBLISH_INTENT") {
          intent.weights.clear();
          intent.preferred_new_fleet_role.reset();
          intent.defer_new_colonization = !intent.defer_new_colonization;
        }
      } else if (fields[2] == "REMOVE") {
        industry.remove(std::stoi(fields[3]));
        shipbuilding.remove(std::stoi(fields[3]));
      } else if (fields[2] == "CLEAR") {
        industry.clear();
        shipbuilding.clear();
      } else require(false, tag + ": unknown operation");
      verify_provider_state(fields, industry, shipbuilding, tag);
      ++provider_operations;
    } else if (fields[0] == "E") {
      require(fields.size() == 5 && fields[2] == "ArgumentNullException" && !decode_hex(fields[3]).empty() && fields[4] == "source-only-null", "source-only row");
      ++source_only;
    } else require(false, "unknown fixture row");
  }
  require(intent_cases == 50 && provider_operations == 10 && formatter_cases == 7 && source_only == 5, "coverage counts");
  std::cout << "validated " << intent_cases << " intent cases, " << provider_operations
            << " provider operations, " << formatter_cases << " formatter cases, and "
            << source_only << " explicitly source-only null records\n";
} catch (const std::exception &error) {
  std::cerr << "FAIL: " << error.what() << '\n';
  return 1;
}
