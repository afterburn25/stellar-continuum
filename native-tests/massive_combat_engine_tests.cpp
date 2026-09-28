#include <stellar/core/massive_combat_engine.hpp>
#include <stellar/core/galaxy_payload_json.hpp>

#define main gate079_massive_combat_persistence_unused_main
#include "massive_combat_persistence_tests.cpp"
#undef main

#include <functional>
#include <typeinfo>

namespace gate093 {

struct ErrorInfo {
  std::string type;
  std::string message;
  std::optional<std::string> parameter;
};

void require(bool condition, std::string message) {
  if (!condition)
    throw std::runtime_error(std::move(message));
}

bool equal_json(const Json &actual, const Json &expected,
                std::string_view path = "$", double tolerance = 1e-10) {
  if (actual.is_number_integer() && expected.is_number_integer())
    return actual.get<std::int64_t>() == expected.get<std::int64_t>();
  if (actual.is_number_unsigned() && expected.is_number_unsigned())
    return actual.get<std::uint64_t>() == expected.get<std::uint64_t>();
  if (actual.is_number_integer() && expected.is_number_unsigned()) {
    const auto left = actual.get<std::int64_t>();
    return left >= 0 && static_cast<std::uint64_t>(left) ==
                            expected.get<std::uint64_t>();
  }
  if (actual.is_number_unsigned() && expected.is_number_integer()) {
    const auto right = expected.get<std::int64_t>();
    return right >= 0 && actual.get<std::uint64_t>() ==
                            static_cast<std::uint64_t>(right);
  }
  if (actual.is_number() && expected.is_number()) {
    const auto left = actual.get<double>();
    const auto right = expected.get<double>();
    constexpr std::string_view float_fields[]{
        "X", "Y", "DamagePerShot", "ShotsPerSecond",
        "Range", "Accuracy", "PowerPerSecond", "HeatPerSecond",
        "MassEach", "PowerPerSecondEach", "HeatPerSecondEach", "Condition",
        "EffectiveRange", "FieldStrength", "DetectionSignature", "MassPerShip",
        "Acceleration", "MaximumSpeed", "ShieldPerShip", "ArmorPerShip",
        "HullPerShip", "ReactorOutputPerShip", "CoolingPerShip",
        "WarpStabilization", "WarpSpoolSeconds", "MaximumModuleMass",
        "HullFraction", "EngineFraction", "SensorFraction", "WarpDriveFraction",
        "ReactorFraction", "InterdictorFraction", "Experience", "Cohesion",
        "Morale", "ShieldPool", "ArmorPool", "HullPool",
        "HullLossThresholdPerShip", "Heat", "PowerReserve",
        "WarpSpoolProgress", "HullDamageRemainder", "Damage",
        "RemainingSeconds", "InitialFlightSeconds"};
    const auto separator = path.find_last_of(".[");
    const auto field = separator == std::string_view::npos
                           ? path
                           : path.substr(separator + 1);
    if (std::ranges::contains(float_fields, field))
      return std::bit_cast<std::uint32_t>(static_cast<float>(left)) ==
             std::bit_cast<std::uint32_t>(static_cast<float>(right));
    return left == right ||
           (std::isfinite(left) && std::isfinite(right) &&
            std::abs(left - right) <= tolerance);
  }
  if (actual.type() != expected.type())
    return false;
  if (actual.is_object()) {
    if (actual.size() != expected.size())
      return false;
    for (const auto &[key, value] : actual.items()) {
      if (!expected.contains(key) ||
          !equal_json(value, expected.at(key), std::string(path) + "." + key,
                      tolerance))
        return false;
    }
    return true;
  }
  if (actual.is_array()) {
    if (actual.size() != expected.size())
      return false;
    for (std::size_t index = 0; index < actual.size(); ++index)
      if (!equal_json(actual[index], expected[index],
                      std::string(path) + "[" + std::to_string(index) + "]",
                      tolerance))
        return false;
    return true;
  }
  return actual == expected;
}

void require_json(const Json &actual, const Json &expected,
                  const std::string &where) {
  if (!equal_json(actual, expected))
    throw std::runtime_error(where + " mismatch\nactual: " + actual.dump() +
                             "\nexpected: " + expected.dump());
}

Json metrics_json(const MassiveCombatMetrics &value) {
  return {{"Tick", value.tick},
          {"ActiveFormations", value.active_formations},
          {"ActiveShips", value.active_ships},
          {"SpatialCells", value.spatial_cells},
          {"TargetCandidatesExamined", value.target_candidates_examined},
          {"WeaponGroupsResolved", value.weapon_groups_resolved},
          {"EventsRetained", value.events_retained}};
}

Json order_result_json(const MassiveCombatOrderResult &value) {
  return {{"Accepted", value.accepted}, {"Message", value.message}};
}

Json error_json(const std::optional<ErrorInfo> &error) {
  if (!error)
    return nullptr;
  return {{"Type", error->type},
          {"Message", error->message},
          {"Parameter", error->parameter ? Json(*error->parameter)
                                          : Json(nullptr)}};
}

std::optional<ErrorInfo> classify(const std::exception &error) {
  if (const auto *range =
          dynamic_cast<const MassiveCombatArgumentRangeError *>(&error))
    return ErrorInfo{"ArgumentOutOfRangeException", range->what(),
                     range->parameter()};
  if (dynamic_cast<const MassiveCombatStateError *>(&error))
    return ErrorInfo{"InvalidOperationException", error.what(), std::nullopt};
  if (dynamic_cast<const std::overflow_error *>(&error))
    return ErrorInfo{"OverflowException", error.what(), std::nullopt};
  return ErrorInfo{"UnexpectedNativeException",
                   std::string(typeid(error).name()) + ": " + error.what(),
                   std::nullopt};
}

MassiveCombatOrder order(const Json &value) {
  MassiveCombatOrder result;
  result.formation_id = value.at("FormationId").get<std::int64_t>();
  result.type =
      static_cast<MassiveCombatOrderType>(value.at("Type").get<int>());
  if (!value.at("TargetFormationId").is_null())
    result.target_formation_id =
        value.at("TargetFormationId").get<std::int64_t>();
  if (!value.at("Objective").is_null())
    result.objective = point(value.at("Objective"));
  if (!value.at("Shape").is_null())
    result.shape =
        static_cast<MassiveFormationShape>(value.at("Shape").get<int>());
  return result;
}

CombatHostilityView hostility(const Json &input) {
  if (input.value("Default", false))
    return {};
  std::vector<std::pair<int, int>> pairs;
  for (const auto &pair : input.at("Pairs"))
    pairs.emplace_back(pair.at(0).get<int>(), pair.at(1).get<int>());
  return [pairs = std::move(pairs)](int first, int second) {
    return std::ranges::find(pairs, std::pair{first, second}) != pairs.end();
  };
}

Json expected_error(const Json &row) {
  if (row.at("ErrorType").is_null())
    return nullptr;
  return {{"Type", row.at("ErrorType")},
          {"Message", row.at("ErrorMessage")},
          {"Parameter", row.at("ErrorParameter")}};
}

void replay_row(const Json &row) {
  const auto name = row.at("Name").get<std::string>();
  const auto operation = row.at("Operation").get<std::string>();
  const auto input_before = row.at("Input");
  std::optional<ErrorInfo> error;
  Json result = nullptr;
  Json after = nullptr;

  if (operation == "Clock") {
    MassiveCombatClock clock;
    require_json({{"SpeedMultiplier", clock.speed_multiplier()}},
                 row.at("Before"), name + ".before");
    const auto speed = number(input_before.at("Speed"));
    const auto real = number(input_before.at("Real"));
    const auto maximum = number(input_before.at("Maximum"));
    try {
      clock.set_speed(speed);
      const auto accepted = clock.accept_frame(real, maximum);
      result = {{"Accepted", floating(accepted)},
                {"SpeedMultiplier", floating(clock.speed_multiplier())}};
    } catch (const std::exception &caught) {
      error = classify(caught);
    }
    after = {{"SpeedMultiplier", floating(clock.speed_multiplier())}};
  } else {
    auto state = battle(row.at("Before"));
    require_json(battle_json(state), row.at("Before"), name + ".before");
    try {
      if (operation == "HasActiveHostilities") {
        MassiveCombatEngine engine(hostility(input_before));
        result = engine.has_active_hostilities(state);
      } else if (operation == "IssueOrder") {
        auto prepared_order = order(input_before.at("Order"));
        MassiveCombatEngine engine;
        if (name == "order-nonhostile-target")
          engine = MassiveCombatEngine(
              [](int first, int second) {
                return (first == 1 && second == 2) ||
                       (first == 2 && second == 1);
              });
        result = order_result_json(engine.issue_order(
            state, input_before.at("CivilizationId").get<int>(),
            std::move(prepared_order)));
      } else if (operation == "Advance") {
        MassiveCombatEngine engine([](int first, int second) {
          return (first == 1 && second == 2) ||
                 (first == 2 && second == 1);
        });
        result = metrics_json(
            engine.advance(state, number(input_before.at("ElapsedSeconds"))));
      } else if (operation == "Continuation") {
        MassiveCombatEngine engine([](int first, int second) {
          return (first == 1 && second == 2) ||
                 (first == 2 && second == 1);
        });
        Json values = Json::array();
        for (const auto &step : input_before.at("Steps"))
          values.push_back(metrics_json(engine.advance(state, number(step))));
        result = std::move(values);
      } else if (operation == "MetricsRetention") {
        MassiveCombatEngine engine([](int first, int second) {
          return (first == 1 && second == 2) ||
                 (first == 2 && second == 1);
        });
        const auto previous = engine.advance(state, 0);
        state.formations.at(input_before.at("InitialShipCountFormationIndex")
                                .get<std::size_t>())
            .initial_ship_count = 0;
        state.formations.at(input_before.at("InvalidCohesionFormationIndex")
                                .get<std::size_t>())
            .cohesion = std::numeric_limits<float>::quiet_NaN();
        try {
          static_cast<void>(engine.advance(
              state, number(input_before.at("ElapsedSeconds"))));
        } catch (const std::exception &caught) {
          error = classify(caught);
        }
        result = {{"Previous", metrics_json(previous)},
                  {"Current", metrics_json(previous)},
                  {"Stable", true}};
      } else {
        throw std::runtime_error("unknown fixture operation " + operation);
      }
    } catch (const std::exception &caught) {
      error = classify(caught);
    }
    after = battle_json(state);
  }

  require_json(input_before, row.at("Input"), name + ".input-ownership");
  require_json(after, row.at("After"), name + ".after");
  require_json(result, row.at("Result"), name + ".result");
  require_json(error_json(error), expected_error(row), name + ".error");
}

void native_probes(const Json &fixture) {
  require(Json(std::int64_t{9'007'199'254'740'993LL}) !=
              Json(std::int64_t{9'007'199'254'740'992LL}),
          "integer comparator regression");

  const auto row = std::ranges::find_if(
      fixture.at("Rows"), [](const Json &value) {
        return value.at("Name") == "hostility-forward-only";
      });
  require(row != fixture.at("Rows").end(), "missing move fixture row");
  auto state = battle(row->at("Before"));
  {
    auto spatial=clone_massive_combat_battle(state);MassiveCombatEngine engine;
    for(auto& formation:spatial.formations){formation.position.z=static_cast<float>(formation.id)*40.f;formation.objective=formation.position;formation.objective.z+=500;formation.order=MassiveCombatOrderType::Advance;}
    GalaxyPayloadV16Dto saved;saved.saved_at_utc="2050-03-21T00:00:00Z";saved.active_combat_encounter=CampaignMassiveEncounter{};saved.active_combat_encounter->battle=spatial;
    auto resumed=decode_galaxy_payload_v16_json(encode_galaxy_payload_v16_json(saved)).active_combat_encounter->battle;
    require(resumed.formations.front().position.z==spatial.formations.front().position.z,"Battle depth lost through JSON persistence");
    const auto before=spatial.formations.front().position.z;
    (void)engine.advance(spatial,.2);(void)engine.advance(resumed,.1);(void)engine.advance(resumed,.1);
    require(spatial.formations.front().position.z!=before,"Authoritative 3D motion stayed planar");
    for(std::size_t i=0;i<spatial.formations.size();++i)require(spatial.formations[i].position.z==resumed.formations[i].position.z,"3D tick batching diverged");
  }
  int calls = 0;
  auto owner = std::make_unique<int>(73);
  MassiveCombatEngine original([&](int first, int second) {
    ++calls;
    return *owner == 73 && first == 1 && second == 2;
  });
  MassiveCombatEngine moved(std::move(original));
  require(moved.has_active_hostilities(state), "moved engine callback");
  MassiveCombatEngine assigned;
  assigned = std::move(moved);
  require(assigned.has_active_hostilities(state),
          "move-assigned engine callback");
  require(calls > 0, "hostility callback invoked");

  const MassiveCombatOrder retained_order{1, MassiveCombatOrderType::Advance,
                                           std::nullopt, MassivePoint{7, 9},
                                           MassiveFormationShape::Wedge};
  const auto retained_copy = retained_order;
  static_cast<void>(assigned.issue_order(state, 1, retained_order));
  require(retained_order.formation_id == retained_copy.formation_id &&
              retained_order.type == retained_copy.type &&
              retained_order.target_formation_id ==
                  retained_copy.target_formation_id &&
              retained_order.objective->x == retained_copy.objective->x &&
              retained_order.objective->y == retained_copy.objective->y &&
              retained_order.shape == retained_copy.shape,
          "owned order changed during state mutation");
}

int run(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: massive_combat_engine_tests <fixture.json> <Game source root>\n";
    return 1;
  }
  const auto fixture_path = fs::absolute(argv[1]).lexically_normal();
  const auto source_root = fs::absolute(argv[2]).lexically_normal();
  try {
    const auto fixture_bytes = read_bytes(fixture_path, "fixture");
    const auto fixture_hash = sha256(fixture_bytes);
    const auto fixture = Json::parse(fixture_bytes);
    require(fixture.at("SchemaVersion") == 1, "fixture schema");
    require(fixture.at("Authority") == "actual C# source", "fixture authority");
    require(fixture.at("SourceOnlyRows") == 0, "source-only accounting");
    require(fixture.at("Rows").size() ==
                fixture.at("RowCount").get<std::size_t>(),
            "row accounting");
    std::vector<std::pair<fs::path, std::string>> source_hashes;
    for (const auto &source : fixture.at("SourceFiles")) {
      const auto path = source_root / source.at("Path").get<std::string>();
      const auto digest = sha256(read_bytes(path, "source file"));
      require(digest == source.at("Sha256").get<std::string>(),
              "source fingerprint " + path.string());
      source_hashes.emplace_back(path, digest);
    }
    std::size_t replayed = 0;
    for (const auto &row : fixture.at("Rows")) {
      replay_row(row);
      ++replayed;
    }
    require(replayed == fixture.at("RowCount").get<std::size_t>(),
            "exact replay accounting");
    native_probes(fixture);
    require(sha256(read_bytes(fixture_path, "fixture")) == fixture_hash,
            "fixture changed during replay");
    for (const auto &[path, digest] : source_hashes)
      require(sha256(read_bytes(path, "source file")) == digest,
              "source changed during replay " + path.string());
    std::cout << "Massive combat engine parity: " << replayed << "/"
              << replayed << " rows passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what() << '\n';
    std::cerr << "Working directory: " << fs::current_path().string() << '\n';
    std::cerr << "Source root: " << source_root.string() << '\n';
    std::cerr << "Fixture path: " << fixture_path.string() << '\n';
    return 1;
  }
}

} // namespace gate093

int main(int argc, char **argv) { return gate093::run(argc, argv); }
