#pragma once
#include <filesystem>
namespace stellar::engine {
// Platform executable location, independent from shell working directory or PATH lookup.
std::filesystem::path executable_directory();
}
