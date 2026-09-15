enum class ResearchRecord {
  campaign,
  civilization,
  v5,
  v4,
  v3,
  v2,
  core,
  node,
  evidence,
  capability,
  context,
  project,
  expertise,
  field,
  competence,
  institution,
  tacit_asset,
  pressure_support,
  agenda,
  orientations,
  foreign_assessment,
  foreign_package,
  outcomes,
  outcome_summary,
  outcome_record,
  funding,
};

void validate_research_record(const OrderedValue &value,
                              const std::string &path, ResearchRecord record);

void validate_research_string(const OrderedValue &value,
                              const std::string &path) {
  (void)typed_string(value, path);
}

void validate_string_list(const OrderedValue &value, const std::string &path) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    return;
  const auto &items = typed_array(value, path);
  for (std::size_t index = 0; index < items.size(); ++index)
    validate_research_string(items[index], path + "/" + std::to_string(index));
}

void validate_record_list(const OrderedValue &value, const std::string &path,
                          ResearchRecord record) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    return;
  const auto &items = typed_array(value, path);
  for (std::size_t index = 0; index < items.size(); ++index)
    validate_research_record(items[index], path + "/" + std::to_string(index),
                             record);
}

enum class ResearchDictionaryValue { number, string, string_list };

void validate_research_dictionary(const OrderedValue &value,
                                  const std::string &path,
                                  ResearchDictionaryValue kind) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    return;
  for (const auto &[key, item] : typed_object(value, path)) {
    const auto item_path = path + "/" + key;
    if (kind == ResearchDictionaryValue::number)
      (void)typed_double(item, item_path);
    else if (kind == ResearchDictionaryValue::string)
      validate_research_string(item, item_path);
    else
      validate_string_list(item, item_path);
  }
}

[[noreturn]] void research_representability_error(const OrderedValue &value,
                                                  const std::string &path) {
  fail(PlayerCampaignJsonStage::Representability,
       "NativeRepresentabilityException",
       "A null source reference cannot be represented by the current typed "
       "Adaptive Research DTO.",
       std::nullopt, std::nullopt, path, value.byte);
}

void check_research_representability(const OrderedValue &value,
                                     const std::string &path,
                                     ResearchRecord record);

void check_research_string(const OrderedValue &value, const std::string &path) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    research_representability_error(value, path);
}

void check_research_string_list(const OrderedValue &value,
                                const std::string &path) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    research_representability_error(value, path);
  const auto &items = std::get<OrderedValue::Array>(value.data);
  for (std::size_t index = 0; index < items.size(); ++index)
    check_research_string(items[index], path + "/" + std::to_string(index));
}

void check_research_record_list(const OrderedValue &value,
                                const std::string &path, ResearchRecord record,
                                bool null_means_empty = false) {
  if (std::holds_alternative<std::nullptr_t>(value.data)) {
    if (null_means_empty)
      return;
    research_representability_error(value, path);
  }
  const auto &items = std::get<OrderedValue::Array>(value.data);
  for (std::size_t index = 0; index < items.size(); ++index) {
    const auto item_path = path + "/" + std::to_string(index);
    if (std::holds_alternative<std::nullptr_t>(items[index].data))
      research_representability_error(items[index], item_path);
    check_research_representability(items[index], item_path, record);
  }
}

void check_research_dictionary(const OrderedValue &value,
                               const std::string &path,
                               ResearchDictionaryValue kind) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    research_representability_error(value, path);
  for (const auto &[key, item] : std::get<OrderedObject>(value.data)) {
    const auto item_path = path + "/" + key;
    if (kind == ResearchDictionaryValue::string)
      check_research_string(item, item_path);
    else if (kind == ResearchDictionaryValue::string_list)
      check_research_string_list(item, item_path);
  }
}

void check_research_representability(const OrderedValue &value,
                                     const std::string &path,
                                     ResearchRecord record) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    research_representability_error(value, path);
  const auto &object = std::get<OrderedObject>(value.data);
  for (std::size_t index = 0; index < object.size(); ++index) {
    const auto &[name, field] = object[index];
    bool superseded = false;
    for (std::size_t later = index + 1; later < object.size(); ++later)
      if (object[later].first == name) {
        superseded = true;
        break;
      }
    if (superseded)
      continue;
    const auto p = path + "/" + name;
    const auto string = [&] { check_research_string(field, p); };
    const auto strings = [&] { check_research_string_list(field, p); };
    const auto child = [&](ResearchRecord type) {
      check_research_representability(field, p, type);
    };
    const auto records = [&](ResearchRecord type,
                             bool null_means_empty = false) {
      check_research_record_list(field, p, type, null_means_empty);
    };

    switch (record) {
    case ResearchRecord::campaign:
      if (name == "CatalogId")
        string();
      else if (name == "Civilizations")
        records(ResearchRecord::civilization);
      break;
    case ResearchRecord::civilization:
      if (name == "SpeciesId" || name == "ReferenceProfileId" ||
          name == "ApplicabilityContextId")
        string();
      else if (name == "Research")
        child(ResearchRecord::v5);
      else if (name == "ProjectFunding")
        records(ResearchRecord::funding, true);
      break;
    case ResearchRecord::v5:
      if (name == "CatalogId")
        string();
      else if (name == "Research")
        child(ResearchRecord::v4);
      else if (name == "Outcomes")
        child(ResearchRecord::outcomes);
      break;
    case ResearchRecord::v4:
      if (name == "CatalogId")
        string();
      else if (name == "Research")
        child(ResearchRecord::v3);
      else if (name == "ForeignAssessments")
        records(ResearchRecord::foreign_assessment);
      else if (name == "ForeignPackages")
        records(ResearchRecord::foreign_package);
      break;
    case ResearchRecord::v3:
      if (name == "CatalogId")
        string();
      else if (name == "Research")
        child(ResearchRecord::v2);
      else if (name == "PressureSupport")
        child(ResearchRecord::pressure_support);
      else if (name == "Agenda")
        child(ResearchRecord::agenda);
      break;
    case ResearchRecord::v2:
      if (name == "CatalogId")
        string();
      else if (name == "Core")
        child(ResearchRecord::core);
      else if (name == "Expertise")
        child(ResearchRecord::expertise);
      break;
    case ResearchRecord::core:
      if (name == "CatalogId" || name == "CivilizationId" ||
          name == "DirectedProgramStageId")
        string();
      else if (name == "Nodes")
        records(ResearchRecord::node);
      else if (name == "Pressures")
        check_research_dictionary(field, p, ResearchDictionaryValue::number);
      else if (name == "Evidence")
        records(ResearchRecord::evidence);
      else if (name == "CivilizationTraits" || name == "FacilityCapabilities" ||
               name == "EnabledDeploymentEventIds")
        strings();
      else if (name == "ApplicabilityContexts")
        check_research_dictionary(field, p,
                                  ResearchDictionaryValue::string_list);
      else if (name == "Capabilities")
        records(ResearchRecord::capability);
      else if (name == "ActiveProjects")
        records(ResearchRecord::project);
      break;
    case ResearchRecord::node:
      if (name == "NodeId")
        string();
      break;
    case ResearchRecord::evidence:
      if (name == "EvidenceInstanceId" || name == "EvidenceTypeId" ||
          name == "Provenance")
        string();
      break;
    case ResearchRecord::capability:
      if (name == "CapabilityId")
        string();
      break;
    case ResearchRecord::context:
      if (name == "ContextId")
        string();
      else if (name == "Traits")
        strings();
      break;
    case ResearchRecord::project:
      if (name == "NodeId")
        string();
      break;
    case ResearchRecord::expertise:
      if (name == "Fields")
        records(ResearchRecord::field);
      else if (name == "Institutions")
        records(ResearchRecord::institution);
      else if (name == "TacitAssets")
        records(ResearchRecord::tacit_asset);
      break;
    case ResearchRecord::field:
      if (name == "FieldId")
        string();
      else if (name == "Current" || name == "HistoricalPeak")
        child(ResearchRecord::competence);
      break;
    case ResearchRecord::competence:
      break;
    case ResearchRecord::institution:
      if (name == "InstitutionInstanceId" || name == "InstitutionArchetypeId")
        string();
      break;
    case ResearchRecord::tacit_asset:
      if (name == "AssetId" || name == "AssetTypeId" || name == "ScopeRef" ||
          name == "Provenance")
        string();
      break;
    case ResearchRecord::pressure_support:
      if (name == "MetricSignals")
        check_research_dictionary(field, p, ResearchDictionaryValue::number);
      else if (name == "ActivePressureIds")
        strings();
      break;
    case ResearchRecord::agenda:
      if (name == "DomainPriorities" || name == "FieldPriorities" ||
          name == "ProblemPriorities" || name == "CapabilityPriorities")
        check_research_dictionary(field, p, ResearchDictionaryValue::string);
      else if (name == "Orientations")
        child(ResearchRecord::orientations);
      else if (name == "CultureAxes")
        check_research_dictionary(field, p, ResearchDictionaryValue::number);
      else if (name == "PolicyProvenance")
        string();
      break;
    case ResearchRecord::orientations:
      break;
    case ResearchRecord::foreign_assessment:
      if (name == "ForeignTechnologyReference" ||
          name == "SourceLineageReference")
        string();
      else if (name == "KnownConstraintIds" || name == "EvidenceRefs" ||
               name == "TacitAssetRefs")
        strings();
      break;
    case ResearchRecord::foreign_package:
      if (name == "PackageId" || name == "ForeignTechnologyReference" ||
          name == "SourceLineageReference" || name == "Provenance")
        string();
      else if (name == "ComponentIds" || name == "RightIds" ||
               name == "EvidenceRefs" || name == "TacitAssetRefs" ||
               name == "KnowledgeFieldIds")
        strings();
      break;
    case ResearchRecord::outcomes:
      if (name == "Summaries")
        records(ResearchRecord::outcome_summary);
      else if (name == "RecentRecords")
        records(ResearchRecord::outcome_record);
      break;
    case ResearchRecord::outcome_summary:
      if (name == "NodeId")
        string();
      break;
    case ResearchRecord::outcome_record:
      if (name == "NodeId" || name == "CheckpointId" || name == "Explanation")
        string();
      break;
    case ResearchRecord::funding:
      if (name == "NodeId")
        string();
      break;
    }
  }
}

void validate_nullable_string(const OrderedValue &value,
                              const std::string &path) {
  (void)typed_optional<std::string>(value, path, typed_string);
}

void validate_nullable_double(const OrderedValue &value,
                              const std::string &path) {
  (void)typed_optional<double>(value, path, typed_double);
}

void validate_research_record(const OrderedValue &value,
                              const std::string &path, ResearchRecord record) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    return;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    const auto integer32 = [&] { (void)typed_integer<int>(field, p); };
    const auto integer64 = [&] { (void)typed_integer<std::int64_t>(field, p); };
    const auto number = [&] { (void)typed_double(field, p); };
    const auto boolean = [&] { (void)typed_bool(field, p); };
    const auto string = [&] { validate_research_string(field, p); };
    const auto child = [&](ResearchRecord type) {
      validate_research_record(field, p, type);
    };
    const auto records = [&](ResearchRecord type) {
      validate_record_list(field, p, type);
    };

    switch (record) {
    case ResearchRecord::campaign:
      if (name == "SchemaVersion")
        integer32();
      else if (name == "CatalogId")
        string();
      else if (name == "Civilizations")
        records(ResearchRecord::civilization);
      break;
    case ResearchRecord::civilization:
      if (name == "CivilizationId")
        integer32();
      else if (name == "SpeciesId" || name == "ReferenceProfileId" ||
               name == "ApplicabilityContextId")
        string();
      else if (name == "Research")
        child(ResearchRecord::v5);
      else if (name == "ProjectFunding")
        records(ResearchRecord::funding);
      break;
    case ResearchRecord::v5:
      if (name == "SchemaVersion")
        integer32();
      else if (name == "CatalogId")
        string();
      else if (name == "Research")
        child(ResearchRecord::v4);
      else if (name == "Outcomes")
        child(ResearchRecord::outcomes);
      break;
    case ResearchRecord::v4:
      if (name == "SchemaVersion")
        integer32();
      else if (name == "CatalogId")
        string();
      else if (name == "Research")
        child(ResearchRecord::v3);
      else if (name == "ForeignAssessments")
        records(ResearchRecord::foreign_assessment);
      else if (name == "ForeignPackages")
        records(ResearchRecord::foreign_package);
      break;
    case ResearchRecord::v3:
      if (name == "SchemaVersion")
        integer32();
      else if (name == "CatalogId")
        string();
      else if (name == "Research")
        child(ResearchRecord::v2);
      else if (name == "PressureSupport")
        child(ResearchRecord::pressure_support);
      else if (name == "Agenda")
        child(ResearchRecord::agenda);
      break;
    case ResearchRecord::v2:
      if (name == "SchemaVersion")
        integer32();
      else if (name == "CatalogId")
        string();
      else if (name == "Core")
        child(ResearchRecord::core);
      else if (name == "Expertise")
        child(ResearchRecord::expertise);
      break;
    case ResearchRecord::core:
      if (name == "SchemaVersion")
        integer32();
      else if (name == "CatalogId" || name == "CivilizationId" ||
               name == "DirectedProgramStageId")
        string();
      else if (name == "TotalEffectiveResearchLabs")
        number();
      else if (name == "Nodes")
        records(ResearchRecord::node);
      else if (name == "Pressures")
        validate_research_dictionary(field, p, ResearchDictionaryValue::number);
      else if (name == "Evidence")
        records(ResearchRecord::evidence);
      else if (name == "CivilizationTraits" || name == "FacilityCapabilities" ||
               name == "EnabledDeploymentEventIds")
        validate_string_list(field, p);
      else if (name == "ApplicabilityContexts")
        validate_research_dictionary(field, p,
                                     ResearchDictionaryValue::string_list);
      else if (name == "Capabilities")
        records(ResearchRecord::capability);
      else if (name == "ActiveProjects")
        records(ResearchRecord::project);
      break;
    case ResearchRecord::node:
      if (name == "NodeId")
        string();
      else if (name == "Maturity")
        integer32();
      else if (name == "Resolution")
        validate_nullable_string(field, p);
      else if (name == "StageResearchPoints" || name == "TotalResearchPoints")
        number();
      break;
    case ResearchRecord::evidence:
      if (name == "EvidenceInstanceId" || name == "EvidenceTypeId" ||
          name == "Provenance")
        string();
      else if (name == "Quality" || name == "Confidence")
        number();
      else if (name == "ContextId")
        validate_nullable_string(field, p);
      break;
    case ResearchRecord::capability:
      if (name == "CapabilityId")
        string();
      else if (name == "ContextId")
        validate_nullable_string(field, p);
      break;
    case ResearchRecord::context:
      if (name == "ContextId")
        string();
      else if (name == "Traits")
        validate_string_list(field, p);
      break;
    case ResearchRecord::project:
      if (name == "NodeId")
        string();
      else if (name == "Stage")
        integer32();
      else if (name == "TargetApplicabilityContextId" || name == "PauseReason")
        validate_nullable_string(field, p);
      else if (name == "AssignedEffectiveLabs" ||
               name == "ReadinessEfficiency" || name == "StageResearchPoints" ||
               name == "TotalResearchPoints")
        number();
      else if (name == "Paused")
        boolean();
      break;
    case ResearchRecord::expertise:
      if (name == "Fields")
        records(ResearchRecord::field);
      else if (name == "Institutions")
        records(ResearchRecord::institution);
      else if (name == "TacitAssets")
        records(ResearchRecord::tacit_asset);
      break;
    case ResearchRecord::field:
      if (name == "FieldId")
        string();
      else if (name == "Current" || name == "HistoricalPeak")
        child(ResearchRecord::competence);
      else if (name == "LastTheoreticalActivityYear" ||
               name == "LastExperimentalActivityYear" ||
               name == "LastEngineeringActivityYear")
        number();
      break;
    case ResearchRecord::competence:
      if (name == "Theoretical" || name == "Experimental" ||
          name == "Engineering")
        number();
      break;
    case ResearchRecord::institution:
      if (name == "InstitutionInstanceId" || name == "InstitutionArchetypeId")
        string();
      else if (name == "ContextId")
        validate_nullable_string(field, p);
      else if (name == "TotalCount" || name == "ActiveCount")
        integer32();
      break;
    case ResearchRecord::tacit_asset:
      if (name == "AssetId" || name == "AssetTypeId" || name == "ScopeRef" ||
          name == "Provenance")
        string();
      else if (name == "ScopeKind" || name == "AssimilationStage")
        integer32();
      else if (name == "Depth" || name == "Availability" ||
               name == "TranslationContextQuality" ||
               name == "TrainingContinuity")
        number();
      else if (name == "ContextId")
        validate_nullable_string(field, p);
      break;
    case ResearchRecord::pressure_support:
      if (name == "MetricSignals")
        validate_research_dictionary(field, p, ResearchDictionaryValue::number);
      else if (name == "ActivePressureIds")
        validate_string_list(field, p);
      break;
    case ResearchRecord::agenda:
      if (name == "DomainPriorities" || name == "FieldPriorities" ||
          name == "ProblemPriorities" || name == "CapabilityPriorities")
        validate_research_dictionary(field, p, ResearchDictionaryValue::string);
      else if (name == "Orientations")
        child(ResearchRecord::orientations);
      else if (name == "CultureAxes")
        validate_research_dictionary(field, p, ResearchDictionaryValue::number);
      else if (name == "LastMajorReviewYear")
        validate_nullable_double(field, p);
      else if (name == "PolicyProvenance")
        string();
      break;
    case ResearchRecord::orientations:
      if (name == "BasicVsAppliedOrientation" ||
          name == "CompetencePreservationPolicy" ||
          name == "PortfolioDiversityPolicy" ||
          name == "ForeignScienceEngagement")
        number();
      break;
    case ResearchRecord::foreign_assessment:
      if (name == "ForeignTechnologyReference" ||
          name == "SourceLineageReference")
        string();
      else if (name == "Understanding" || name == "Operability" ||
               name == "Reproduction" || name == "Adaptation")
        integer32();
      else if (name == "KnownConstraintIds" || name == "EvidenceRefs" ||
               name == "TacitAssetRefs")
        validate_string_list(field, p);
      else if (name == "LastAssessmentYear" || name == "Confidence")
        number();
      break;
    case ResearchRecord::foreign_package:
      if (name == "PackageId" || name == "ForeignTechnologyReference" ||
          name == "SourceLineageReference" || name == "Provenance")
        string();
      else if (name == "ComponentIds" || name == "RightIds" ||
               name == "EvidenceRefs" || name == "TacitAssetRefs" ||
               name == "KnowledgeFieldIds")
        validate_string_list(field, p);
      else if (name == "Integrity")
        number();
      break;
    case ResearchRecord::outcomes:
      if (name == "Summaries")
        records(ResearchRecord::outcome_summary);
      else if (name == "RecentRecords")
        records(ResearchRecord::outcome_record);
      break;
    case ResearchRecord::outcome_summary:
      if (name == "NodeId")
        string();
      else if (name == "Attempts" || name == "Setbacks" ||
               name == "PartialSuccesses" || name == "Refinements" ||
               name == "Disproofs" || name == "Anomalies" ||
               name == "Hazards" || name == "SideDiscoveries")
        integer32();
      else if (name == "LastOutcomeId")
        validate_nullable_string(field, p);
      else if (name == "LastOutcomeYear")
        number();
      break;
    case ResearchRecord::outcome_record:
      if (name == "Sequence")
        integer64();
      else if (name == "NodeId" || name == "CheckpointId" ||
               name == "Explanation")
        string();
      else if (name == "AttemptIndex" || name == "Outcome")
        integer32();
      else if (name == "SideDiscoveryNodeId")
        validate_nullable_string(field, p);
      else if (name == "Year")
        number();
      break;
    case ResearchRecord::funding:
      if (name == "NodeId")
        string();
      else if (name == "ReservedMilestoneCredits" ||
               name == "ConsumedMilestoneCredits" ||
               name == "AuthorizationCredits")
        number();
      break;
    }
  }
}
