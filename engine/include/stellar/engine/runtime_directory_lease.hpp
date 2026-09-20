#pragma once
#include <filesystem>
namespace stellar::engine {
// The game and maintenance tools share this lease. It closes the launch race
// between checking for a running game and beginning a file transaction.
class RuntimeDirectoryLease {
public:
  explicit RuntimeDirectoryLease(const std::filesystem::path& directory);
  ~RuntimeDirectoryLease();
  RuntimeDirectoryLease(const RuntimeDirectoryLease&)=delete;
  RuntimeDirectoryLease& operator=(const RuntimeDirectoryLease&)=delete;
private:
  void* handle_{};
};
}
