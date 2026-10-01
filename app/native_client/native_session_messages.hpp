#pragma once

#include <stellar/engine/localization.hpp>

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_session {

// Session-notice localization shim. NativeCampaignSession keeps stable English
// literals/skeletons (save/load failure heads and restoration progress
// statuses); the render boundary resolves them through the active locale table
// and falls back to the original message whenever a key is absent.
[[nodiscard]] inline std::string localized_session_message(
    const stellar::engine::LocalizationTable *locale, std::string_view message) {
  if (!locale || message.empty()) return std::string(message);
  const auto tr = [&](std::string_view key, std::string_view fallback) {
    return locale->contains(key) ? std::string(locale->translate(key))
                                 : std::string(fallback);
  };
  const auto trf = [&](std::string_view key, std::string_view head,
                       std::string_view fallback) {
    const std::string detail{message.substr(head.size())};
    if (locale->contains(key)) {
      const std::vector<std::string> values{detail};
      return locale->format(key, std::span<const std::string>(values));
    }
    std::string out{fallback};
    if (const auto at = out.find("{0}"); at != std::string::npos)
      out.replace(at, 3, detail);
    return out;
  };
  const auto *statics = [](std::string_view text) -> const char * {
    if (text == "Reading saved campaign") return "SESSION_LOAD_READING";
    if (text == "Decoding saved campaign") return "SESSION_LOAD_DECODING";
    if (text == "Restoring diplomacy") return "SESSION_LOAD_DIPLOMACY";
    if (text == "Restoring galaxy state") return "SESSION_LOAD_GALAXY";
    if (text == "Validating campaign references")
      return "SESSION_LOAD_REFERENCES";
    if (text == "Restoring research progress") return "SESSION_LOAD_RESEARCH";
    if (text == "Finalizing restored campaign") return "SESSION_LOAD_FINALIZING";
    if (text == "Primary save unavailable; restoring backup")
      return "SESSION_LOAD_BACKUP_FALLBACK";
    if (text == "Restoring an older autosave") return "SESSION_LOAD_HISTORY";
    return nullptr;
  }(message);
  if (statics) return tr(statics, message);
  if (message.starts_with("Save failed: "))
    return trf("SESSION_NOTICE_SAVE_FAILED", "Save failed: ", message);
  if (message.starts_with("Autosave failed: "))
    return trf("SESSION_NOTICE_AUTOSAVE_FAILED", "Autosave failed: ", message);
  if (message.starts_with("Load failed: "))
    return trf("SESSION_NOTICE_LOAD_FAILED", "Load failed: ", message);
  return std::string(message);
}

} // namespace stellar::native_session
