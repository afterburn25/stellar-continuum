#include "galaxy_payload_test_helpers.hpp"

#define main gate087_embedded_main
#include "galaxy_payload_persistence_tests.cpp"
#undef main

#include <stellar/core/galaxy_payload_json.hpp>

#include <iostream>

namespace {

GalaxyPayloadJsonErrorPhase phase(const std::string &name) {
  if (name == "Parse") return GalaxyPayloadJsonErrorPhase::Parse;
  if (name == "RequiredMember") return GalaxyPayloadJsonErrorPhase::RequiredMember;
  if (name == "DateTimeOffset") return GalaxyPayloadJsonErrorPhase::DateTimeOffset;
  if (name == "Representability") return GalaxyPayloadJsonErrorPhase::Representability;
  if (name == "Encode") return GalaxyPayloadJsonErrorPhase::Encode;
  throw std::runtime_error("Unknown native expectation phase '" + name + "'.");
}

void compare_json_shape(const Json &actual, const Json &expected,
                        const std::string &path) {
  if (actual.is_number() && expected.is_number()) return;
  check(actual.type() == expected.type(), path + ": JSON value kind");
  if (actual.is_object()) {
    check(actual.size() == expected.size(), path + ": property count");
    for (const auto &[name, value] : expected.items()) {
      const auto found = actual.find(name);
      check(found != actual.end(), path + ": missing property " + name);
      compare_json_shape(*found, value, path + '.' + name);
    }
  } else if (actual.is_array()) {
    check(actual.size() == expected.size(), path + ": array size");
    for (std::size_t index = 0; index != actual.size(); ++index)
      compare_json_shape(actual[index], expected[index],
                         path + '[' + std::to_string(index) + ']');
  } else {
    check(actual == expected, path + ": JSON scalar");
  }
}

void verify_source_hashes(const Json &fixture) {
  check(fixture.at("SourceHashesBefore") == fixture.at("SourceHashesAfter"),
        "authoritative source hashes changed during generation");
  check(fixture.at("Gate087FixtureSha256").get<std::string>() ==
            "8217317A7AA1FE8873FAF76B4FAF2FD04E1A6BB61251D647A168E69EE33C056B",
        "Gate087 fixture SHA changed");
}

std::string run_rows(const Json &fixture) {
  std::size_t successes{};
  std::size_t native_errors{};
  std::size_t source_deserialize_errors{};
  std::size_t source_serialize_errors{};
  std::string baseline_encoded;
  const auto &baseline_row = *std::ranges::find_if(
      fixture.at("Rows"), [](const Json &row) {
        return row.at("Name") == "field-complete-current16";
      });
  const auto baseline_canonical =
      Json::parse(baseline_row.at("SourceCanonicalJson").get<std::string>());

  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    auto input = row.at("InputJson").get<std::string>();
    const auto expected_phase =
        row.at("NativeExpectation").at("Phase").get<std::string>();
    const auto expected_path = row.at("NativeExpectation").at("Path");
    source_deserialize_errors += !row.at("SourceDeserializeError").is_null();
    source_serialize_errors += !row.at("SourceSerializeError").is_null();
    if (!row.at("SourceDeserializeError").is_null())
      check(row.at("SourceDeserializeError").at("Type") == "JsonException",
            name + ": actual source deserialize category");
    if (!row.at("SourceSerializeError").is_null())
      check(row.at("SourceSerializeError").at("Type") ==
                    "ArgumentException" ||
                row.at("SourceSerializeError").at("Type") ==
                    "NullReferenceException",
            name + ": actual source serialize category");

    std::optional<GalaxyPayloadV16Dto> decoded;
    std::optional<GalaxyPayloadJsonError> decode_error;
    try {
      decoded = decode_galaxy_payload_v16_json(input);
    } catch (const GalaxyPayloadJsonError &error) {
      decode_error = error;
    } catch (const std::exception &error) {
      throw std::runtime_error(name + ": unexpected decode exception: " +
                               error.what());
    }

    if (expected_phase != "Success" && expected_phase != "Encode") {
      check(decode_error.has_value(), name + ": expected decode failure");
      check(decode_error->phase() == phase(expected_phase),
            name + ": decode phase");
      if (!expected_path.is_null())
        check(decode_error->path() == expected_path.get<std::string>(),
              name + ": decode path");
      ++native_errors;
      continue;
    }

    check(decoded.has_value() && !decode_error.has_value(),
          name + ": unexpected decode failure");
    const auto canonical = row.at("SourceCanonicalJson");
    if (canonical.is_null()) {
      check(expected_phase == "Encode",
            name + ": source-accepted non-finite projection category");
      auto finite_projection = *decoded;
      if (name == "double-overflow" || name == "huge-positive-exponent") {
        check(std::isinf(finite_projection.simulation_days),
              name + ": source-accepted non-finite double");
        finite_projection.simulation_days = 37.25;
      } else if (name == "single-overflow") {
        check(finite_projection.systems &&
                  !finite_projection.systems->empty() &&
                  std::isinf(finite_projection.systems->front().x),
              name + ": source-accepted non-finite single");
        finite_projection.systems->front().x = 0;
      } else {
        throw std::runtime_error(name + ": unknown non-finite fixture row");
      }
      check_payload(finite_projection, baseline_canonical,
                    name + ": decoded payload except non-finite value");
    } else {
      check_payload(*decoded, Json::parse(canonical.get<std::string>()),
                    name + ": decoded payload");
    }
    if (name == "negative-zero-double" ||
        name == "huge-negative-exponent" ||
        name == "negative-zero-huge-negative-exponent")
      check(decoded->simulation_days == 0 &&
                std::signbit(decoded->simulation_days),
            name + ": negative-zero sign retained");
    if (name == "zero-huge-positive-exponent")
      check(decoded->simulation_days == 0 &&
                !std::signbit(decoded->simulation_days),
            name + ": positive-zero sign retained");

    const auto retained = *decoded;
    input.assign("mutated source storage");
    if (canonical.is_null()) {
      check(((name == "double-overflow" ||
              name == "huge-positive-exponent") &&
             std::isinf(retained.simulation_days)) ||
                (name == "single-overflow" && retained.systems &&
                 !retained.systems->empty() &&
                 std::isinf(retained.systems->front().x)),
            name + ": detached non-finite value");
    } else {
      check_payload(retained, Json::parse(canonical.get<std::string>()),
                    name + ": detached ownership");
    }

    try {
      const auto encoded = encode_galaxy_payload_v16_json(retained);
      check(expected_phase == "Success", name + ": expected encode failure");
      const auto expected_json = Json::parse(canonical.get<std::string>());
      compare_json_shape(Json::parse(encoded), expected_json, name);
      check_payload(decode_galaxy_payload_v16_json(encoded), expected_json,
                    name + ": encoded typed values");
      check(encoded == encode_galaxy_payload_v16_json(retained),
            name + ": deterministic encoding");
      if (name == "field-complete-current16") baseline_encoded = encoded;
    } catch (const GalaxyPayloadJsonError &error) {
      check(expected_phase == "Encode", name + ": unexpected encode failure");
      check(error.phase() == GalaxyPayloadJsonErrorPhase::Encode,
            name + ": encode phase");
      if (!expected_path.is_null())
        check(error.path() == expected_path.get<std::string>(),
              name + ": encode path");
    } catch (const std::exception &error) {
      throw std::runtime_error(name + ": unexpected encode exception: " +
                               error.what());
    }

    if (!row.at("ExpectedRestored").is_null()) {
      const auto restored = restore_galaxy_payload_v16(retained);
      check_raw_world(restored.galaxy, row.at("ExpectedRestored"),
                      name + ": actual-source restore");
      check(restored.simulation_days == retained.simulation_days &&
                restored.game_version == retained.game_version,
            name + ": restored envelope values");
      check_payload(retained, Json::parse(canonical.get<std::string>()),
                    name + ": input unchanged by restore");
    }
    if (expected_phase == "Success") ++successes;
    else ++native_errors;
  }

  check(successes == 27, "exact native success row accounting");
  check(native_errors == 31, "exact native error row accounting");
  check(source_deserialize_errors == 16,
        "exact source deserialize error accounting");
  check(source_serialize_errors == 4,
        "exact source serialize error accounting");
  check(successes + native_errors == fixture.at("RowCount").get<std::size_t>(),
        "all fixture rows accounted");
  check(!baseline_encoded.empty(), "baseline encoded output captured");
  return baseline_encoded;
}

void verify_native_encode_failures(const Json &fixture) {
  const auto &baseline_row = *std::ranges::find_if(
      fixture.at("Rows"), [](const Json &row) {
        return row.at("Name") == "field-complete-current16";
      });
  const auto baseline = decode_galaxy_payload_v16_json(
      baseline_row.at("SourceCanonicalJson").get<std::string>());
  const auto expect_encode = [](const GalaxyPayloadV16Dto &payload,
                                const std::string &path) {
    try {
      (void)encode_galaxy_payload_v16_json(payload);
      throw std::runtime_error("Expected a native encode failure at " + path);
    } catch (const GalaxyPayloadJsonError &error) {
      check(error.phase() == GalaxyPayloadJsonErrorPhase::Encode,
            path + ": native encode phase");
      check(error.path() == path, path + ": native encode path");
    }
  };

  auto bad_timestamp = baseline;
  bad_timestamp.saved_at_utc = "not-a-timestamp";
  expect_encode(bad_timestamp, "$.SavedAtUtc");

  auto nested_nonfinite = baseline;
  check(nested_nonfinite.systems && !nested_nonfinite.systems->empty(),
        "native non-finite system probe setup");
  nested_nonfinite.systems->front().x =
      std::numeric_limits<float>::quiet_NaN();
  expect_encode(nested_nonfinite, "$.Galaxy.Systems[0].X");

  auto metadata_nonfinite = baseline;
  metadata_nonfinite.generation_metadata.emplace();
  metadata_nonfinite.generation_metadata->created_at_utc =
      "2042-03-04T05:06:07+00:00";
  metadata_nonfinite.generation_metadata->galactic_core =
      GalacticCoreMetadata{"core", std::numeric_limits<float>::infinity(),
                           0, 0};
  expect_encode(metadata_nonfinite,
                "$.Galaxy.GenerationMetadata.GalacticCore.X");

  auto invalid_utf8 = baseline;
  invalid_utf8.game_version = std::string(1, static_cast<char>(0xc3));
  expect_encode(invalid_utf8, "$");
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "Expected the actual-source fixture and native output paths.");
    const auto fixture_path = fs::absolute(argv[1]);
    const auto fixture_bytes = bytes(fixture_path);
    check(sha256(fixture_bytes) ==
              "BE0C842BC3D6473EBA58FA36EFB3D55AB18CB5CD1C95344A027DBC1C1BA691F8",
          "actual-source fixture SHA changed");
    const auto fixture = Json::parse(fixture_bytes);
    check(fixture.at("Schema") ==
              "stellar.galaxy-json-codec.actual-source.v1",
          "fixture schema");
    verify_source_hashes(fixture);
    const auto native_output = run_rows(fixture);
    verify_native_encode_failures(fixture);
    const auto output_path = fs::absolute(argv[2]);
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    check(bool(output), "Cannot open native interoperability output '" +
                            output_path.string() + "'.");
    output.write(native_output.data(),
                 static_cast<std::streamsize>(native_output.size()));
    check(bool(output), "Cannot write native interoperability output '" +
                            output_path.string() + "'.");
    output.close();
    check(bool(output), "Cannot close native interoperability output '" +
                            output_path.string() + "'.");
    std::cout << "Galaxy payload JSON codec: 58 actual-source rows passed "
                 "(27 success, 31 native errors; 16 source decode errors, "
                 "4 source encode errors).\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Galaxy payload JSON codec failure: " << typeid(error).name()
              << ": " << error.what() << '\n';
    std::cerr << "cwd: " << fs::current_path() << '\n';
    std::cerr << "fixture: "
              << (argc > 1 ? fs::absolute(argv[1]).string() : "<missing>")
              << '\n';
    std::cerr << "native output: "
              << (argc > 2 ? fs::absolute(argv[2]).string() : "<missing>")
              << '\n';
    return 1;
  }
}
