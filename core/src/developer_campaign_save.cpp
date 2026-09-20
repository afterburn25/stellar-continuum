#include <stellar/core/developer_campaign_save.hpp>

#include <stellar/core/developer_campaign_json.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/save_history.hpp>
#include <stellar/engine/save_integrity.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <span>
#include <stdexcept>
#include <utility>

namespace stellar::core {
namespace {

void require_save_path(const std::filesystem::path &path) {
  const auto &native = path.native();
  if (native.empty() ||
      std::all_of(native.begin(), native.end(), [](auto c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
               c == '\v';
      }))
    throw std::invalid_argument("A save path is required.");
}

} // namespace

void write_prepared_developer_campaign(
    const std::filesystem::path &path,
    const PreparedPlayerCampaignSave &prepared,
    const bool preserve_existing_backup) {
  require_save_path(path);
  if (!prepared.is_developer())
    throw std::invalid_argument(
        "A Developer campaign save requires a Developer-envelope capture.");
  const auto json =
      encode_developer_campaign_json(prepared.payload(), prepared.tools_used());
  const auto bytes = std::as_bytes(std::span(json.data(), json.size()));
  if (!preserve_existing_backup)
    stellar::engine::rotate_save_history(path);
  stellar::engine::write_file_atomically(path, bytes,
                                         {preserve_existing_backup});
  stellar::engine::write_integrity_sidecar(path, bytes);
  stellar::engine::write_history_sidecars(path);
}

} // namespace stellar::core
