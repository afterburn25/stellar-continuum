#pragma once

#include <stellar/engine/diagnostic_bundle.hpp>
#include <vector>
#include <filesystem>
#include <string>

namespace stellar::native_support {

// Plain app snapshots supplied by the owner thread. This utility performs no
// logging, environment probing, or filesystem work until export is requested.
struct SupportBundleRequest final {
  std::filesystem::path user_root;
  std::filesystem::path save_path;
  std::string system_info;
  std::string session_log;
  std::vector<stellar::engine::DiagnosticBundleEntry> additional_entries{};
  std::string archive_name{"support.zip"};
};

// Writes a store-format ZIP below user_root/support/<unique>/support.zip.
// It includes session.log and system.txt, plus campaign.player17.json only
// when save_path names an existing regular file. Throws on all failures so the
// caller can report an asynchronous export failure without ending the game.
[[nodiscard]] std::filesystem::path
export_support_bundle(const SupportBundleRequest& request);

} // namespace stellar::native_support
