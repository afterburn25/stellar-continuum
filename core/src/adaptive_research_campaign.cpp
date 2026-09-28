#include <stellar/core/adaptive_research_campaign.hpp>

#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {

bool consume_dotnet_whitespace(std::string_view &value) noexcept {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t point = first;
  std::size_t width = 1;
  if ((first & 0xe0) == 0xc0) {
    point = first & 0x1f;
    width = 2;
  } else if ((first & 0xf0) == 0xe0) {
    point = first & 0x0f;
    width = 3;
  } else if ((first & 0xf8) == 0xf0) {
    point = first & 7;
    width = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < width)
    return false;
  for (std::size_t index = 1; index < width; ++index) {
    const auto byte = static_cast<unsigned char>(value[index]);
    if ((byte & 0xc0) != 0x80)
      return false;
    point = (point << 6) | (byte & 0x3f);
  }
  value.remove_prefix(width);
  return (point >= 0x09 && point <= 0x0d) || point == 0x20 || point == 0x85 ||
         point == 0xa0 || point == 0x1680 ||
         (point >= 0x2000 && point <= 0x200a) || point == 0x2028 ||
         point == 0x2029 || point == 0x202f || point == 0x205f ||
         point == 0x3000;
}

bool blank(std::string_view value) noexcept {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value))
      return false;
  return true;
}

std::vector<std::uint16_t> utf16(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t point{};
    std::size_t width{};
    if (first <= 0x7f) {
      point = first;
      width = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      point = first & 0x1f;
      width = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      point = first & 0x0f;
      width = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      point = first & 7;
      width = 4;
    } else {
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    }
    if (value.size() < width)
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    for (std::size_t index = 1; index < width; ++index) {
      const auto byte = static_cast<unsigned char>(value[index]);
      if ((byte & 0xc0) != 0x80)
        throw std::invalid_argument("Research identifier is not valid UTF-8.");
      point = (point << 6) | (byte & 0x3f);
    }
    value.remove_prefix(width);
    if (point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(point));
    } else {
      point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (point & 0x3ff)));
    }
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16(left) < utf16(right);
}

} // namespace

struct AdaptiveResearchCampaignState::Storage {
  struct FundingCollection {
    std::vector<std::optional<AdaptiveResearchProjectFundingState>> slots;
    std::unordered_map<std::string, std::size_t> index;
    std::vector<std::size_t> free;
    mutable std::vector<AdaptiveResearchProjectFundingState> view;
    mutable bool view_dirty{true};

    const AdaptiveResearchProjectFundingState *find(std::string_view id) const {
      const auto found = index.find(std::string(id));
      return found == index.end() ? nullptr : &*slots[found->second];
    }

    bool add(AdaptiveResearchProjectFundingState value) {
      if (index.contains(value.node_id))
        return false;
      const auto key = value.node_id;
      std::size_t position{};
      if (free.empty()) {
        position = slots.size();
        slots.emplace_back(std::move(value));
      } else {
        position = free.back();
        free.pop_back();
        slots[position] = std::move(value);
      }
      index.emplace(key, position);
      view_dirty = true;
      return true;
    }

    void replace(std::string_view id,
                 AdaptiveResearchProjectFundingState value) {
      slots[index.at(std::string(id))] = std::move(value);
      view_dirty = true;
    }

    void erase(std::string_view id) {
      const auto found = index.find(std::string(id));
      if (found == index.end())
        return;
      const auto position = found->second;
      index.erase(found);
      slots[position].reset();
      free.push_back(position);
      view_dirty = true;
    }

    std::span<const AdaptiveResearchProjectFundingState> values() const {
      if (!view_dirty)
        return view;
      view.clear();
      view.reserve(index.size());
      for (const auto &slot : slots)
        if (slot)
          view.push_back(*slot);
      view_dirty = false;
      return view;
    }
  };

  struct CivilizationEntry {
    int id{};
    AdaptiveResearchCivilizationState state;
    AdaptiveResearchCivilizationStart start;
    FundingCollection funding;
    AdaptiveResearchPlan plan;
  };

  const AdaptiveResearchStrategicRuntime *runtime{};
  std::vector<CivilizationEntry> civilizations;
  std::vector<int> ids;
  std::unordered_map<int, std::size_t> index;

  explicit Storage(const AdaptiveResearchStrategicRuntime &owner)
      : runtime(&owner) {}

  CivilizationEntry *find(int id) noexcept {
    const auto found = index.find(id);
    return found == index.end() ? nullptr : &civilizations[found->second];
  }
  const CivilizationEntry *find(int id) const noexcept {
    const auto found = index.find(id);
    return found == index.end() ? nullptr : &civilizations[found->second];
  }

  void add(int id, AdaptiveResearchCivilizationState state,
           AdaptiveResearchCivilizationStart start) {
    index.emplace(id, civilizations.size());
    ids.push_back(id);
    civilizations.push_back(
        CivilizationEntry{id, std::move(state), std::move(start), {}});
  }
};

AdaptiveResearchCampaignMissingState::AdaptiveResearchCampaignMissingState(
    std::string message)
    : std::out_of_range(std::move(message)) {}
AdaptiveResearchCampaignDataError::AdaptiveResearchCampaignDataError(
    std::string message)
    : std::runtime_error(std::move(message)) {}
AdaptiveResearchCampaignOperationError::AdaptiveResearchCampaignOperationError(
    std::string message)
    : std::runtime_error(std::move(message)) {}
AdaptiveResearchCampaignArgumentError::AdaptiveResearchCampaignArgumentError(
    std::string message)
    : std::invalid_argument(std::move(message)) {}

AdaptiveResearchCampaignState::AdaptiveResearchCampaignState(
    std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
AdaptiveResearchCampaignState::~AdaptiveResearchCampaignState() = default;
AdaptiveResearchCampaignState::AdaptiveResearchCampaignState(
    AdaptiveResearchCampaignState &&) noexcept = default;
AdaptiveResearchCampaignState &AdaptiveResearchCampaignState::operator=(
    AdaptiveResearchCampaignState &&) noexcept = default;

const AdaptiveResearchStrategicRuntime &
AdaptiveResearchCampaignState::runtime() const noexcept {
  return *storage_->runtime;
}
std::span<const int>
AdaptiveResearchCampaignState::civilization_ids() const noexcept {
  return storage_->ids;
}
const AdaptiveResearchCivilizationState *
AdaptiveResearchCampaignState::try_get_civilization(int id) const noexcept {
  const auto *entry = storage_->find(id);
  return entry ? &entry->state : nullptr;
}
const AdaptiveResearchCivilizationState &
AdaptiveResearchCampaignState::get_civilization(int id) const {
  if (const auto *state = try_get_civilization(id))
    return *state;
  throw AdaptiveResearchCampaignMissingState(
      "Campaign has no Adaptive Research state for civilization " +
      std::to_string(id) + ".");
}
const AdaptiveResearchCivilizationStart *
AdaptiveResearchCampaignState::try_get_start(int id) const noexcept {
  const auto *entry = storage_->find(id);
  return entry ? &entry->start : nullptr;
}
const AdaptiveResearchCivilizationStart &
AdaptiveResearchCampaignState::get_start(int id) const {
  if (const auto *start = try_get_start(id))
    return *start;
  throw AdaptiveResearchCampaignMissingState(
      "Campaign has no Adaptive Research start for civilization " +
      std::to_string(id) + ".");
}
std::span<const AdaptiveResearchProjectFundingState>
AdaptiveResearchCampaignState::project_funding(int id) const {
  const auto *entry = storage_->find(id);
  if (!entry)
    throw AdaptiveResearchCampaignMissingState(
        "Campaign has no Adaptive Research funding state for civilization " +
        std::to_string(id) + ".");
  return entry->funding.values();
}

const AdaptiveResearchPlan &AdaptiveResearchCampaignState::plan(int id) const {
  (void)get_civilization(id);
  return storage_->find(id)->plan;
}

AdaptiveResearchCommandResult AdaptiveResearchCampaignState::edit_plan(
    int id, ResearchPlanCommand command, std::string_view node_id) {
  auto *entry=storage_->find(id);
  if(!entry)return AdaptiveResearchCommandResult::rejected("Research civilization is unavailable.");
  auto next=entry->plan;
  if(command==ResearchPlanCommand::SuggestionsOn||command==ResearchPlanCommand::SuggestionsOff)
    next.suggestions=command==ResearchPlanCommand::SuggestionsOn;
  else {
    const auto *node=entry->state.try_get_node_state(node_id);
    if(!node||node->maturity<ResearchMaturity::investigable)
      return AdaptiveResearchCommandResult::rejected("That technology has not been discovered.");
    const bool favorite=command==ResearchPlanCommand::AddFavorite||command==ResearchPlanCommand::RemoveFavorite;
    auto &list=favorite?next.favorites:next.queue;
    auto found=std::find(list.begin(),list.end(),node_id);
    if(command==ResearchPlanCommand::AddFavorite||command==ResearchPlanCommand::Enqueue){
      if(command==ResearchPlanCommand::Enqueue&&(node->maturity>=ResearchMaturity::mature||
          std::any_of(entry->state.active_projects().begin(),entry->state.active_projects().end(),
            [&](const auto &p){return p.node_id==node_id;})))
        return AdaptiveResearchCommandResult::rejected("This program is already active or concluded.");
      if(found==list.end()){
        if(list.size()>=128)return AdaptiveResearchCommandResult::rejected("Research plan is full (128 entries).");
        list.emplace_back(node_id);
      }
    }else if(command==ResearchPlanCommand::RemoveFavorite||command==ResearchPlanCommand::RemoveQueued){
      if(found!=list.end())list.erase(found);
    }else if(command==ResearchPlanCommand::MoveUp||command==ResearchPlanCommand::MoveDown){
      if(found==list.end()||(command==ResearchPlanCommand::MoveUp&&found==list.begin())||
         (command==ResearchPlanCommand::MoveDown&&found+1==list.end()))
        return AdaptiveResearchCommandResult::rejected("The queued program cannot move in that direction.");
      std::iter_swap(found,command==ResearchPlanCommand::MoveUp?found-1:found+1);
    }else return AdaptiveResearchCommandResult::rejected("Unknown research plan command.");
  }
  if(next!=entry->plan){
    detail::AdaptiveResearchStateWriter::mark_state_changed(entry->state);
    entry->plan=std::move(next);
  }
  return {true,"Research plan updated. Queued programs start in order when time is running and their requirements are met.",{}, {}};
}

AdaptiveResearchCampaignFactory::AdaptiveResearchCampaignFactory(
    const AdaptiveResearchStrategicRuntime &runtime) noexcept
    : runtime_(&runtime) {}

std::string_view AdaptiveResearchCampaignFactory::select_reference_profile(
    std::string_view species_id) {
  if (species_id == "terran_baseline")
    return terran_profile_id;
  if (species_id == "pelagic_high_pressure")
    return pelagic_profile_id;
  if (species_id == "compact_high_gravity")
    return high_gravity_profile_id;
  if (species_id == "cryogenic_hydrocarbon")
    return cryogenic_hydrocarbon_profile_id;
  throw AdaptiveResearchCampaignMissingState(
      "No Adaptive Research starting profile is registered for species '" +
      std::string(species_id) + "'.");
}

AdaptiveResearchCampaignState AdaptiveResearchCampaignFactory::create(
    std::span<const Civilization> civilizations) const {
  std::vector<const Civilization *> ordered;
  ordered.reserve(civilizations.size());
  for (const auto &civilization : civilizations)
    ordered.push_back(&civilization);
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto *left,
                                                      const auto *right) {
    return left->id < right->id;
  });

  auto storage = std::make_unique<AdaptiveResearchCampaignState::Storage>(*runtime_);
  storage->civilizations.reserve(ordered.size());
  storage->ids.reserve(ordered.size());
  for (const auto *civilization : ordered) {
    if (storage->index.contains(civilization->id))
      throw AdaptiveResearchCampaignOperationError(
          "Duplicate civilization ID " + std::to_string(civilization->id) +
          " cannot own research state.");
    try {
      (void)species_environment_profile(civilization->species_id);
    } catch (const std::out_of_range &) {
      throw AdaptiveResearchCampaignMissingState(
          "Unknown species ID '" + civilization->species_id + "'.");
    }
    const auto profile = select_reference_profile(civilization->species_id);
    const auto civilization_id =
        "civilization:" + std::to_string(civilization->id);
    const auto context_id = "species:" + civilization->species_id;
    auto composition = runtime_->authority().compose_reference_profile(
        civilization_id, std::string(profile), context_id);
    storage->add(
        civilization->id, std::move(composition.state),
        {civilization->id, civilization->species_id, std::string(profile),
         context_id});
  }
  return AdaptiveResearchCampaignState(std::move(storage));
}

AdaptiveResearchCampaignState AdaptiveResearchCampaignFactory::create(
    const FreshCampaignState &campaign) const {
  return create(campaign.civilizations);
}

struct AdaptiveResearchCampaignSnapshotCodec::Storage {
  const AdaptiveResearchStrategicRuntime *runtime{};
  AdaptiveResearchOutcomeSnapshotCodec civilization_codec;

  explicit Storage(const AdaptiveResearchStrategicRuntime &owner)
      : runtime(&owner), civilization_codec(owner) {}
};

AdaptiveResearchCampaignSnapshotCodec::AdaptiveResearchCampaignSnapshotCodec(
    const AdaptiveResearchStrategicRuntime &runtime)
    : storage_(std::make_unique<Storage>(runtime)) {}
AdaptiveResearchCampaignSnapshotCodec::~AdaptiveResearchCampaignSnapshotCodec() =
    default;
AdaptiveResearchCampaignSnapshotCodec::AdaptiveResearchCampaignSnapshotCodec(
    AdaptiveResearchCampaignSnapshotCodec &&) noexcept = default;
AdaptiveResearchCampaignSnapshotCodec &
AdaptiveResearchCampaignSnapshotCodec::operator=(
    AdaptiveResearchCampaignSnapshotCodec &&) noexcept = default;

AdaptiveResearchCampaignSnapshot
AdaptiveResearchCampaignSnapshotCodec::capture(
    const AdaptiveResearchCampaignState &campaign) const {
  if (&campaign.runtime() != storage_->runtime)
    throw AdaptiveResearchCampaignOperationError(
        "Adaptive Research campaign belongs to a different runtime catalog "
        "instance.");
  AdaptiveResearchCampaignSnapshot result{
      2,
      storage_->runtime->authority().catalog().metadata().catalog_id,
      {}};
  std::vector<int> ordered(campaign.civilization_ids().begin(),
                           campaign.civilization_ids().end());
  std::sort(ordered.begin(), ordered.end());
  result.civilizations.reserve(ordered.size());
  for (const auto id : ordered) {
    const auto &start = campaign.get_start(id);
    std::vector<AdaptiveResearchProjectFundingSnapshot> funding;
    for (const auto &value : campaign.project_funding(id))
      funding.push_back({value.node_id, value.reserved_milestone_credits,
                         value.consumed_milestone_credits,
                         value.authorization_credits});
    std::sort(funding.begin(), funding.end(), [](const auto &left,
                                                 const auto &right) {
      return ordinal_less(left.node_id, right.node_id);
    });
    result.civilizations.push_back(
        {id, start.species_id, start.reference_profile_id,
         start.applicability_context_id,
         storage_->civilization_codec.capture(campaign.get_civilization(id)),
         std::move(funding), campaign.plan(id)});
    if(campaign.plan(id)!=AdaptiveResearchPlan{})result.schema_version=current_schema_version;
  }
  return result;
}

AdaptiveResearchCampaignState AdaptiveResearchCampaignSnapshotCodec::restore(
    std::span<const Civilization> civilizations,
    const AdaptiveResearchCampaignSnapshot &snapshot) const {
  if (snapshot.schema_version < 1 ||
      snapshot.schema_version > current_schema_version)
    throw AdaptiveResearchCampaignDataError(
        "Unsupported Adaptive Research campaign schema " +
        std::to_string(snapshot.schema_version) + ".");
  const auto &catalog_id =
      storage_->runtime->authority().catalog().metadata().catalog_id;
  if (snapshot.catalog_id != catalog_id)
    throw AdaptiveResearchCampaignDataError(
        "Adaptive Research campaign catalog '" + snapshot.catalog_id +
        "' does not match runtime catalog '" + catalog_id + "'.");

  std::unordered_map<int, const Civilization *> galaxy;
  galaxy.reserve(civilizations.size());
  for (const auto &civilization : civilizations)
    if (!galaxy.emplace(civilization.id, &civilization).second)
      throw AdaptiveResearchCampaignArgumentError(
          "An item with the same key has already been added. Key: " +
          std::to_string(civilization.id));
  if (snapshot.civilizations.size() != galaxy.size())
    throw AdaptiveResearchCampaignDataError(
        "Adaptive Research campaign must contain exactly one state for every "
        "civilization.");

  auto campaign_storage =
      std::make_unique<AdaptiveResearchCampaignState::Storage>(*storage_->runtime);
  campaign_storage->civilizations.reserve(snapshot.civilizations.size());
  campaign_storage->ids.reserve(snapshot.civilizations.size());
  for (const auto &entry : snapshot.civilizations) {
    const auto found = galaxy.find(entry.civilization_id);
    if (found == galaxy.end())
      throw AdaptiveResearchCampaignDataError(
          "Adaptive Research references unknown civilization " +
          std::to_string(entry.civilization_id) + ".");
    if (campaign_storage->index.contains(entry.civilization_id))
      throw AdaptiveResearchCampaignDataError(
          "Adaptive Research duplicates civilization " +
          std::to_string(entry.civilization_id) + ".");
    const auto &civilization = *found->second;
    const auto expected_profile =
        AdaptiveResearchCampaignFactory::select_reference_profile(
            civilization.species_id);
    const auto expected_context = "species:" + civilization.species_id;
    if (entry.species_id != civilization.species_id ||
        entry.reference_profile_id != expected_profile ||
        entry.applicability_context_id != expected_context)
      throw AdaptiveResearchCampaignDataError(
          "Adaptive Research identity metadata does not match civilization " +
          std::to_string(entry.civilization_id) + ".");
    auto state = storage_->civilization_codec.restore(entry.research);
    const auto expected_state_id =
        "civilization:" + std::to_string(entry.civilization_id);
    if (state.civilization_id() != expected_state_id)
      throw AdaptiveResearchCampaignDataError(
          "Adaptive Research state identity does not match civilization " +
          std::to_string(entry.civilization_id) + ".");
    campaign_storage->add(
        entry.civilization_id, std::move(state),
        {entry.civilization_id, entry.species_id, entry.reference_profile_id,
         entry.applicability_context_id});
  }
  AdaptiveResearchCampaignState campaign(std::move(campaign_storage));
  if (snapshot.schema_version >= 2)
    for (const auto &entry : snapshot.civilizations)
      detail::AdaptiveResearchCampaignStateAccess::restore_project_funding(
          campaign, entry.civilization_id, entry.project_funding);
  for(const auto &entry:snapshot.civilizations){
    if(snapshot.schema_version<3&&entry.plan!=AdaptiveResearchPlan{})
      throw AdaptiveResearchCampaignDataError("Research planning requires campaign research schema 3.");
    detail::AdaptiveResearchCampaignStateAccess::restore_plan(campaign,entry.civilization_id,entry.plan);
  }
  return campaign;
}

AdaptiveResearchCampaignState AdaptiveResearchCampaignSnapshotCodec::restore(
    const FreshCampaignState &campaign,
    const AdaptiveResearchCampaignSnapshot &snapshot) const {
  return restore(campaign.civilizations, snapshot);
}

namespace detail {

void AdaptiveResearchCampaignStateAccess::restore_plan(
    AdaptiveResearchCampaignState &campaign,int id,const AdaptiveResearchPlan &plan){
  const auto owned=plan;
  const auto &state=campaign.get_civilization(id);
  for(const auto *list:{&owned.favorites,&owned.queue}){
    if(list->size()>128)throw AdaptiveResearchCampaignDataError("Research plan exceeds 128 entries.");
    std::vector<std::string> seen;
    for(const auto &node_id:*list){
      const auto *node=state.try_get_node_state(node_id);
      if(node_id.size()>128||!campaign.runtime().authority().catalog().find_node(node_id)||
         !node||node->maturity<ResearchMaturity::investigable||
         std::find(seen.begin(),seen.end(),node_id)!=seen.end())
        throw AdaptiveResearchCampaignDataError("Research plan contains an unknown, undiscovered or duplicate technology.");
      seen.push_back(node_id);
    }
  }
  campaign.storage_->find(id)->plan=owned;
}

AdaptiveResearchCivilizationState &
AdaptiveResearchCampaignStateAccess::get_civilization(
    AdaptiveResearchCampaignState &campaign, int civilization_id) {
  auto *entry = campaign.storage_->find(civilization_id);
  if (!entry)
    throw AdaptiveResearchCampaignMissingState(
        "Campaign has no Adaptive Research state for civilization " +
        std::to_string(civilization_id) + ".");
  return entry->state;
}

void AdaptiveResearchCampaignStateAccess::reserve_project_milestones(
    AdaptiveResearchCampaignState &campaign, int civilization_id,
    std::string_view node_id, double authorization_credits,
    double milestone_credits) {
  const std::string owned_node(node_id);
  if (!std::isfinite(authorization_credits) || authorization_credits < 0)
    throw std::out_of_range("authorizationCredits");
  if (!std::isfinite(milestone_credits) || milestone_credits < 0)
    throw std::out_of_range("milestoneCredits");
  auto *entry = campaign.storage_->find(civilization_id);
  if (!entry)
    throw AdaptiveResearchCampaignMissingState(
        "Campaign has no Adaptive Research funding state for civilization " +
        std::to_string(civilization_id) + ".");
  if (!entry->funding.add(
          {owned_node, milestone_credits, 0, authorization_credits}))
    throw AdaptiveResearchCampaignOperationError(
        "Research project '" + owned_node +
        "' already has milestone funding.");
}

AdaptiveResearchMilestoneConsumption
AdaptiveResearchCampaignStateAccess::consume_project_milestone(
    AdaptiveResearchCampaignState &campaign, int civilization_id,
    std::string_view node_id, bool final_milestone) {
  const std::string owned_node(node_id);
  auto *entry = campaign.storage_->find(civilization_id);
  if (!entry)
    throw AdaptiveResearchCampaignMissingState(
        "Campaign has no Adaptive Research funding state for civilization " +
        std::to_string(civilization_id) + ".");
  const auto *current = entry->funding.find(owned_node);
  if (!current)
    return {};
  auto remaining = std::max(
      0.0, current->reserved_milestone_credits -
               current->consumed_milestone_credits);
  const auto consumed =
      final_milestone
          ? remaining
          : std::min(current->reserved_milestone_credits / 3.0, remaining);
  remaining = std::max(0.0, remaining - consumed);
  if (final_milestone || remaining <= 0.000001) {
    entry->funding.erase(owned_node);
  } else {
    auto replacement = *current;
    replacement.consumed_milestone_credits += consumed;
    entry->funding.replace(owned_node, std::move(replacement));
  }
  return {true, consumed, remaining};
}

void AdaptiveResearchCampaignStateAccess::release_project_funding(
    AdaptiveResearchCampaignState &campaign, int civilization_id, std::string_view node_id) {
  auto *entry = campaign.storage_->find(civilization_id);
  if (!entry) throw AdaptiveResearchCampaignMissingState("Missing research civilization.");
  entry->funding.erase(node_id);
}

void AdaptiveResearchCampaignStateAccess::restore_project_funding(
    AdaptiveResearchCampaignState &campaign, int civilization_id,
    std::span<const AdaptiveResearchProjectFundingSnapshot> snapshots) {
  const std::vector<AdaptiveResearchProjectFundingSnapshot> owned(
      snapshots.begin(), snapshots.end());
  auto *entry = campaign.storage_->find(civilization_id);
  if (!entry)
    throw AdaptiveResearchCampaignMissingState(
        "Campaign has no Adaptive Research funding state for civilization " +
        std::to_string(civilization_id) + ".");
  for (const auto &snapshot : owned) {
    if (blank(snapshot.node_id) ||
        !std::isfinite(snapshot.reserved_milestone_credits) ||
        snapshot.reserved_milestone_credits < 0 ||
        !std::isfinite(snapshot.consumed_milestone_credits) ||
        snapshot.consumed_milestone_credits < 0 ||
        snapshot.consumed_milestone_credits >
            snapshot.reserved_milestone_credits + 0.000001 ||
        !std::isfinite(snapshot.authorization_credits) ||
        snapshot.authorization_credits < 0)
      throw AdaptiveResearchCampaignDataError(
          "Adaptive Research contains invalid project milestone funding.");
    bool active = false;
    for (const auto &project : entry->state.active_projects())
      if (project.node_id == snapshot.node_id) {
        active = true;
        break;
      }
    if (!active)
      throw AdaptiveResearchCampaignDataError(
          "Adaptive Research milestone funding references inactive project '" +
          snapshot.node_id + "'.");
    if (!entry->funding.add({snapshot.node_id,
                             snapshot.reserved_milestone_credits,
                             snapshot.consumed_milestone_credits,
                             snapshot.authorization_credits}))
      throw AdaptiveResearchCampaignDataError(
          "Adaptive Research duplicates milestone funding for '" +
          snapshot.node_id + "'.");
  }
}

} // namespace detail
} // namespace stellar::core
