#pragma once

namespace stellar::core {

struct CampaignDeveloperProvenance {
  bool tools_used{};

  bool operator==(const CampaignDeveloperProvenance &) const = default;
};

} // namespace stellar::core
