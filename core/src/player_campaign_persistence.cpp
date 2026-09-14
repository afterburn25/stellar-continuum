#include <stellar/core/player_campaign_persistence.hpp>

#include <stellar/core/diplomacy_snapshot_invariants.hpp>
#include <stellar/core/detail/player_campaign_persistence_restore.hpp>

#include <cmath>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {

[[noreturn]] void unknown_civilization(int id, const std::string &owner) {
  throw PlayerCampaignPersistenceDataError(
      "Diplomacy " + owner + " references unknown civilization " +
      std::to_string(id) + ".");
}

[[noreturn]] void unknown_system(int id, const std::string &owner) {
  throw PlayerCampaignPersistenceDataError(
      "Diplomacy " + owner + " references unknown star system " +
      std::to_string(id) + ".");
}

} // namespace

PlayerCampaignPersistenceDataError::PlayerCampaignPersistenceDataError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

PlayerCampaignPersistenceDataError::PlayerCampaignPersistenceDataError(
    std::string message, std::string inner_type, std::string inner_message)
    : std::runtime_error(std::move(message)),
      inner_type_(std::move(inner_type)),
      inner_message_(std::move(inner_message)) {}

const std::optional<std::string> &
PlayerCampaignPersistenceDataError::inner_type() const noexcept {
  return inner_type_;
}

const std::optional<std::string> &
PlayerCampaignPersistenceDataError::inner_message() const noexcept {
  return inner_message_;
}

void DiplomacyCampaignReferenceValidator::validate(
    const FreshCampaignState &galaxy,
    const DiplomacyStateSnapshot &snapshot) {
  std::unordered_set<int> civilizations;
  civilizations.reserve(galaxy.civilizations.size());
  for (const auto &civilization : galaxy.civilizations)
    civilizations.insert(civilization.id);
  std::unordered_set<int> systems;
  systems.reserve(galaxy.systems.size());
  for (const auto &system : galaxy.systems)
    systems.insert(system.id);

  const auto require_civilization = [&](int id, const std::string &owner) {
    if (!civilizations.contains(id))
      unknown_civilization(id, owner);
  };
  const auto require_system = [&](int id, const std::string &owner) {
    if (!systems.contains(id))
      unknown_system(id, owner);
  };

  for (const auto &contact : snapshot.contacts) {
    const auto prefix = "contact " + contact.contact_id;
    require_civilization(contact.observer_civilization_id,
                         prefix + " observer");
    if (contact.target_civilization_id)
      require_civilization(*contact.target_civilization_id,
                           prefix + " target");
    if (contact.last_observed_system_id)
      require_system(*contact.last_observed_system_id,
                     prefix + " observed system");
  }
  for (const auto &relationship : snapshot.relationships) {
    require_civilization(relationship.civilization_a_id,
                         "relationship civilization A");
    require_civilization(relationship.civilization_b_id,
                         "relationship civilization B");
    for (const auto &grievance : relationship.grievances)
      require_civilization(grievance.source_civilization_id,
                           "grievance source");
  }
  for (const auto &access : snapshot.access_permissions) {
    require_civilization(access.grantor_civilization_id, "access grantor");
    require_civilization(access.visitor_civilization_id, "access visitor");
  }
  for (const auto &claim : snapshot.claims) {
    const auto prefix = "claim " + std::to_string(claim.claim_id);
    require_civilization(claim.claimant_civilization_id,
                         prefix + " claimant");
    require_system(claim.system_id, prefix + " system");
    for (const int audience : claim.known_to_civilization_ids)
      require_civilization(audience, prefix + " audience");
  }
  for (const auto &response : snapshot.claim_responses)
    require_civilization(response.responding_civilization_id,
                         "claim " + std::to_string(response.claim_id) +
                             " responder");
  for (const auto &agreement : snapshot.agreements) {
    const auto prefix =
        "agreement " + std::to_string(agreement.agreement_id);
    require_civilization(agreement.civilization_a_id,
                         prefix + " civilization A");
    require_civilization(agreement.civilization_b_id,
                         prefix + " civilization B");
  }
  for (const auto &proposal : snapshot.proposals) {
    const auto prefix = "proposal " + std::to_string(proposal.proposal_id);
    require_civilization(proposal.proposer_civilization_id,
                         prefix + " proposer");
    require_civilization(proposal.recipient_civilization_id,
                         prefix + " recipient");
  }
  for (const auto &event : snapshot.recent_history) {
    const auto prefix = "history event " + std::to_string(event.event_id);
    require_civilization(event.primary_civilization_id,
                         prefix + " primary civilization");
    if (event.secondary_civilization_id)
      require_civilization(*event.secondary_civilization_id,
                           prefix + " secondary civilization");
    if (event.system_id)
      require_system(*event.system_id, prefix + " system");
    for (const int audience : event.known_to_civilization_ids)
      require_civilization(audience, prefix + " audience");
  }
}

struct RestoredPlayerCampaignV17::Storage {
  AdaptiveResearchStrategicRuntime research_runtime;
  FreshCampaignState galaxy;
  std::unique_ptr<AdaptiveResearchCampaignState> research;
  std::unique_ptr<DiplomacyState> diplomacy;
  double simulation_days{};
  std::string game_version;
  std::string saved_at_utc;

  Storage(AdaptiveResearchStrategicRuntime runtime,
          RestoredGalaxyPayloadV16 restored,
          const AdaptiveResearchCampaignSnapshot &research_snapshot,
          const DiplomacyStateSnapshot &diplomacy_snapshot,
          const PlayerCampaignRestoreHooks &hooks)
      : research_runtime(std::move(runtime)),
        galaxy(std::move(restored.galaxy)),
        simulation_days(restored.simulation_days),
        game_version(std::move(restored.game_version)),
        saved_at_utc(std::move(restored.saved_at_utc)) {
    research = std::make_unique<AdaptiveResearchCampaignState>(
        AdaptiveResearchCampaignSnapshotCodec(research_runtime)
            .restore(galaxy, research_snapshot));
    if (hooks.before_diplomacy_restore)
      hooks.before_diplomacy_restore();
    diplomacy = std::make_unique<DiplomacyState>(
        DiplomacyState::restore(diplomacy_snapshot));
  }
};

RestoredPlayerCampaignV17::RestoredPlayerCampaignV17(
    std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
RestoredPlayerCampaignV17::~RestoredPlayerCampaignV17() = default;
RestoredPlayerCampaignV17::RestoredPlayerCampaignV17(
    RestoredPlayerCampaignV17 &&) noexcept = default;
RestoredPlayerCampaignV17 &RestoredPlayerCampaignV17::operator=(
    RestoredPlayerCampaignV17 &&) noexcept = default;

FreshCampaignState &RestoredPlayerCampaignV17::galaxy() noexcept {
  return storage_->galaxy;
}
const FreshCampaignState &RestoredPlayerCampaignV17::galaxy() const noexcept {
  return storage_->galaxy;
}
DiplomacyState &RestoredPlayerCampaignV17::diplomacy() noexcept {
  return *storage_->diplomacy;
}
const DiplomacyState &RestoredPlayerCampaignV17::diplomacy() const noexcept {
  return *storage_->diplomacy;
}
AdaptiveResearchCampaignState &RestoredPlayerCampaignV17::research() noexcept {
  return *storage_->research;
}
const AdaptiveResearchCampaignState &
RestoredPlayerCampaignV17::research() const noexcept {
  return *storage_->research;
}
const AdaptiveResearchStrategicRuntime &
RestoredPlayerCampaignV17::research_runtime() const noexcept {
  return storage_->research_runtime;
}
double RestoredPlayerCampaignV17::simulation_days() const noexcept {
  return storage_->simulation_days;
}
std::string_view RestoredPlayerCampaignV17::game_version() const noexcept {
  return storage_->game_version;
}
std::string_view RestoredPlayerCampaignV17::saved_at_utc() const noexcept {
  return storage_->saved_at_utc;
}

IntegratedAdaptiveCampaignRuntime RestoredPlayerCampaignV17::activate() && {
  auto owned = std::move(storage_);
  const auto research_snapshot =
      AdaptiveResearchCampaignSnapshotCodec(owned->research_runtime)
          .capture(*owned->research);
  owned->research.reset();
  DiplomacyState diplomacy = std::move(*owned->diplomacy);
  owned->diplomacy.reset();
  return IntegratedAdaptiveCampaignRuntime::restore_research(
      std::move(owned->research_runtime), std::move(owned->galaxy),
      research_snapshot, std::move(diplomacy), owned->simulation_days);
}

RestoredPlayerCampaignV17 detail::finalize_restored_player_campaign_v17(
    AdaptiveResearchStrategicRuntime research_runtime,
    RestoredGalaxyPayloadV16 restored_galaxy,
    std::function<AdaptiveResearchCampaignSnapshot()> decode_research,
    const DiplomacyStateSnapshot &diplomacy_snapshot,
    const PlayerCampaignRestoreHooks &hooks) {
  if (hooks.before_diplomacy_references)
    hooks.before_diplomacy_references();
  DiplomacyCampaignReferenceValidator::validate(restored_galaxy.galaxy,
                                                 diplomacy_snapshot);
  if (hooks.before_research_restore)
    hooks.before_research_restore();
  auto research_snapshot = decode_research();
  return RestoredPlayerCampaignV17(
      std::make_unique<RestoredPlayerCampaignV17::Storage>(
          std::move(research_runtime), std::move(restored_galaxy),
          research_snapshot, diplomacy_snapshot, hooks));
}

PlayerCampaignPayloadV17Dto capture_player_campaign_v17(
    IntegratedAdaptiveCampaignRuntime &campaign,
    const PlayerCampaignCaptureOptions &options) {
  auto &galaxy = campaign.world().campaign();
  if (galaxy.developer_provenance)
    throw PlayerCampaignPersistenceOperationError(
        "A Developer campaign cannot be written as a Player save. Use "
        "Developer campaign persistence.");
  if (!std::isfinite(options.simulation_days) ||
      options.simulation_days < 0.0)
    throw PlayerCampaignPersistenceRangeError(
        "Simulation time must be finite and non-negative. (Parameter "
        "'simulationDays')");

  const auto diplomacy = campaign.diplomacy().snapshot();
  (void)DiplomacySnapshotInvariantValidator::validate(diplomacy);
  DiplomacyCampaignReferenceValidator::validate(galaxy, diplomacy);
  auto galaxy_payload = capture_galaxy_payload_v16(
      galaxy, {options.simulation_days, options.game_version,
               options.saved_at_utc, false});
  if (galaxy_payload.format_version !=
      GalaxyPayloadV16Dto::current_format_version)
    throw PlayerCampaignPersistenceDataError(
        "Expected galaxy payload format 16, got " +
        std::to_string(galaxy_payload.format_version) + ".");
  auto research =
      AdaptiveResearchCampaignSnapshotCodec(campaign.research_runtime())
          .capture(campaign.research());
  return {PlayerCampaignPayloadV17Dto::current_format_version,
          GalaxyPayloadV16Dto::current_format_version,
          std::move(galaxy_payload), diplomacy, std::move(research)};
}

RestoredPlayerCampaignV17 restore_player_campaign_v17(
    AdaptiveResearchStrategicRuntime research_runtime,
    const PlayerCampaignPayloadV17Dto &payload) {
  if (payload.format_version < 1 ||
      payload.format_version > PlayerCampaignPayloadV17Dto::current_format_version)
    throw PlayerCampaignPersistenceDataError(
        "Unsupported campaign save format " +
        std::to_string(payload.format_version) +
        "; maximum supported is 17.");
  if (payload.format_version !=
      PlayerCampaignPayloadV17Dto::current_format_version)
    throw PlayerCampaignPersistenceDataError(
        "Current typed Player campaign persistence requires format 17.");
  if (!payload.diplomacy)
    throw PlayerCampaignPersistenceDataError(
        "Format v17 save is missing the authoritative Diplomacy snapshot.");
  try {
    (void)DiplomacySnapshotInvariantValidator::validate(*payload.diplomacy);
  } catch (const DiplomacySnapshotValidationError &error) {
    throw PlayerCampaignPersistenceDataError(
        "Format v17 Diplomacy snapshot failed strict invariant validation.",
        "DiplomacySnapshotValidationException", error.what());
  }
  if (!payload.galaxy_format_version)
    throw PlayerCampaignPersistenceDataError(
        "Format v17 save is missing GalaxyFormatVersion.");
  if (*payload.galaxy_format_version !=
      GalaxyPayloadV16Dto::current_format_version)
    throw PlayerCampaignPersistenceDataError(
        "Format v17 save references unsupported galaxy format " +
        std::to_string(*payload.galaxy_format_version) + ".");

  auto galaxy_payload = payload.galaxy;
  galaxy_payload.format_version = *payload.galaxy_format_version;
  auto restored = restore_galaxy_payload_v16(galaxy_payload);
  return detail::finalize_restored_player_campaign_v17(
      std::move(research_runtime), std::move(restored),
      [&payload] {
        if (!payload.adaptive_research)
          throw PlayerCampaignPersistenceDataError(
              "Format v17 save is missing Adaptive Research state.");
        return *payload.adaptive_research;
      },
      *payload.diplomacy);
}

} // namespace stellar::core
