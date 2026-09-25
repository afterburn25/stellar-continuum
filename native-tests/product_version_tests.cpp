// ProductVersion: strict four-number + channel parsing, numeric-then-
// channel precedence ordering, and string round-trip.

#include <stellar/engine/product_version.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *label) {
  if (!condition) {
    std::cerr << "FAIL: " << label << '\n';
    ++failures;
  }
}

bool throws(const char *value) {
  try {
    (void)stellar::engine::ProductVersion::parse(value);
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

} // namespace

int main() {
  using stellar::engine::ProductVersion;

  const auto v = ProductVersion::parse("1.2.3.4-beta");
  check(v.number[0] == 1 && v.number[3] == 4 &&
            v.channel == ProductVersion::Channel::beta,
        "parse reads the four numbers and channel");
  check(v.string() == "1.2.3.4-beta", "string() round-trips");
  check(ProductVersion::parse(v.string()) == v,
        "parse(string()) is an identity");

  // Numeric precedence first, channel rank at equal numbers:
  // dev < beta < stable.
  check(ProductVersion::parse("1.0.0.0-dev") <
            ProductVersion::parse("1.0.0.0-beta"),
        "dev precedes beta at equal numbers");
  check(ProductVersion::parse("1.0.0.0-beta") <
            ProductVersion::parse("1.0.0.0-stable"),
        "beta precedes stable at equal numbers");
  check(ProductVersion::parse("2.0.0.0-dev") >
            ProductVersion::parse("1.9.9.9-stable"),
        "a newer dev build outranks an older stable");

  check(throws("1.2.3.4"), "a channel suffix is required");
  check(throws("1.2.3.4-rc"), "unknown channels are rejected");
  check(throws("1.2.3-dev"), "fewer than four numbers are rejected");
  check(throws("1.2.3.4.5-dev"), "extra numbers are rejected");
  check(throws("1.02.3.4-dev"), "leading zeros are rejected");
  check(throws("1.2.3.65536-dev"), "numbers above 65535 are rejected");
  check(throws("1.2.x.4-dev"), "non-numeric fields are rejected");

  if (failures == 0) std::cout << "product version tests passed\n";
  return failures == 0 ? 0 : 1;
}
