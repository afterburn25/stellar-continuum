#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace stellar::engine {

// Platform-services abstraction (Steam-style). The game talks to this
// interface only; standalone builds use the null backend and keep working
// unchanged. A real Steamworks backend can be plugged in without touching
// gameplay code. No part of this layer fakes a live connection: when the
// backend is absent, status() reports availability=false and feature calls
// return their "unsupported" results.

enum class PlatformFeature {
  Overlay,
  Achievements,
  Stats,
  RichPresence,
  CloudSaves,
  Workshop,
};

struct PlatformUser {
  bool valid{};
  std::uint64_t id{};
  std::string display_name;
};

struct PlatformStatus {
  bool available{};
  std::string backend_name{"none"};
  PlatformUser user;
};

struct FeatureResult {
  bool ok{};
  std::string detail;
};

class PlatformServicesBackend {
public:
  virtual ~PlatformServicesBackend() = default;
  virtual std::string name() const = 0;
  virtual bool available() const = 0;
  virtual PlatformUser user() const = 0;
  virtual bool supports(PlatformFeature feature) const = 0;
  virtual FeatureResult unlock_achievement(const std::string &id) = 0;
  virtual FeatureResult
  set_rich_presence(const std::string &key, const std::string &value) = 0;
  virtual FeatureResult request_cloud_sync() = 0;
};

// Default backend for standalone builds: reports nothing available and makes
// every feature call a safe no-op returning ok=false with a reason string.
class NullPlatformBackend final : public PlatformServicesBackend {
public:
  std::string name() const override { return "null"; }
  bool available() const override { return false; }
  PlatformUser user() const override { return {}; }
  bool supports(PlatformFeature) const override { return false; }
  FeatureResult unlock_achievement(const std::string &) override {
    return {false, "no platform backend"};
  }
  FeatureResult set_rich_presence(const std::string &,
                                  const std::string &) override {
    return {false, "no platform backend"};
  }
  FeatureResult request_cloud_sync() override {
    return {false, "no platform backend"};
  }
};

// Facade owned by the application. Never null internally — detached state is
// the NullPlatformBackend — so callers never branch on backend presence.
class PlatformServices {
public:
  PlatformServices();
  void attach_backend(std::unique_ptr<PlatformServicesBackend> backend);
  void detach_backend(); // returns to the null backend

  PlatformStatus status() const;
  bool supports(PlatformFeature feature) const;
  FeatureResult unlock_achievement(const std::string &id);
  FeatureResult set_rich_presence(const std::string &key,
                                  const std::string &value);
  FeatureResult request_cloud_sync();

private:
  std::unique_ptr<PlatformServicesBackend> backend_;
};

} // namespace stellar::engine
