#include <stellar/engine/platform_services.hpp>

namespace stellar::engine {

PlatformServices::PlatformServices()
    : backend_(std::make_unique<NullPlatformBackend>()) {}

void PlatformServices::attach_backend(
    std::unique_ptr<PlatformServicesBackend> backend) {
  backend_ = backend ? std::move(backend)
                     : std::make_unique<NullPlatformBackend>();
}

void PlatformServices::detach_backend() {
  backend_ = std::make_unique<NullPlatformBackend>();
}

PlatformStatus PlatformServices::status() const {
  PlatformStatus status;
  status.backend_name = backend_->name();
  status.available = backend_->available();
  status.user = backend_->user();
  return status;
}

bool PlatformServices::supports(PlatformFeature feature) const {
  return backend_->supports(feature);
}
FeatureResult PlatformServices::unlock_achievement(const std::string &id) {
  return backend_->unlock_achievement(id);
}
FeatureResult
PlatformServices::set_rich_presence(const std::string &key,
                                    const std::string &value) {
  return backend_->set_rich_presence(key, value);
}
FeatureResult PlatformServices::request_cloud_sync() {
  return backend_->request_cloud_sync();
}

} // namespace stellar::engine
