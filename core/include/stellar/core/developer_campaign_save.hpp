#pragma once

#include <stellar/core/player_campaign_save.hpp>

#include <filesystem>

namespace stellar::core {

// Source: Game.Persistence.DeveloperCampaignPersistenceService —
// WritePreparedDeveloper. The prepared save must carry the Developer
// envelope markers (PreparedPlayerCampaignSave::capture_developer).
void write_prepared_developer_campaign(
    const std::filesystem::path &path,
    const PreparedPlayerCampaignSave &prepared,
    bool preserve_existing_backup);

} // namespace stellar::core
