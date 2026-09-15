using System.Globalization;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2)
{
    Console.Error.WriteLine("usage: ResearchAuthorityOracle <research-data> <fixture>");
    return 1;
}
try
{
    var options = new JsonSerializerOptions
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
    var root = Path.GetFullPath(args[0]);
    string Fingerprint(string directory)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (
            var path in Directory
                .GetFiles(directory, "*.json")
                .OrderBy(Path.GetFileName, StringComparer.Ordinal)
        )
        {
            hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
            hash.AppendData(File.ReadAllBytes(path));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }
    var beforeFingerprint = Fingerprint(root);
    var authority = AdaptiveResearchAuthority.LoadFromDirectory(root);
    var coreMethods = typeof(AdaptiveResearchCivilizationState)
        .GetMethods(BindingFlags.Instance | BindingFlags.NonPublic)
        .GroupBy(m => (m.Name, m.GetParameters().Length))
        .ToDictionary(g => g.Key, g => g.Single());
    void Core(AdaptiveResearchCivilizationState state, string name, params object?[] values)
    {
        try
        {
            _ = coreMethods[(name, values.Length)].Invoke(state, values);
        }
        catch (TargetInvocationException e) when (e.InnerException is not null)
        {
            ExceptionDispatchInfo.Capture(e.InnerException).Throw();
        }
    }
    object ExpertiseSnapshot(AdaptiveResearchExpertiseState e) =>
        new
        {
            e.Revision,
            FieldCompetence = e.FieldCompetence.Values.ToArray(),
            Institutions = e.Institutions.Values.ToArray(),
            TacitAssets = e.TacitAssets.Values.ToArray(),
        };
    object StateSnapshot(AdaptiveResearchCivilizationState s) =>
        new
        {
            s.CivilizationId,
            s.Revision,
            s.MaterializedViewRevision,
            s.DirectedProgramStageId,
            s.TotalEffectiveResearchLabs,
            s.AssignedEffectiveLabs,
            s.FreeEffectiveLabs,
            NodeStates = s.NodeStates.Values.ToArray(),
            Pressures = s.Pressures.Select(p => new { Id = p.Key, p.Value }).ToArray(),
            EvidenceInstances = s.EvidenceInstances.Values.ToArray(),
            CivilizationTraits = s.CivilizationTraits.ToArray(),
            ApplicabilityContexts = s
                .ApplicabilityContexts.Select(p => new { Id = p.Key, Traits = p.Value.ToArray() })
                .ToArray(),
            Capabilities = s.Capabilities.ToArray(),
            FacilityCapabilities = s.FacilityCapabilities.ToArray(),
            EnabledDeploymentEventIds = s.EnabledDeploymentEventIds.ToArray(),
            ActiveProjects = s.ActiveProjects.Values.ToArray(),
            Expertise = ExpertiseSnapshot(s.Expertise),
        };
    object DeferredSnapshot(AdaptiveResearchDeferredStartingState d) =>
        new
        {
            FieldCompetence = d
                .FieldCompetence.Select(p => new
                {
                    Id = p.Key,
                    p.Value.Theoretical,
                    p.Value.Experimental,
                    p.Value.Engineering,
                })
                .ToArray(),
            d.ResearchInstitutions,
            d.TacitAssets,
            d.SelectedFragmentIds,
            d.ReferenceProfileId,
            d.HistoricalNotes,
        };
    object CompositionSnapshot(AdaptiveResearchStartingCompositionResult r) =>
        new
        {
            State = StateSnapshot(r.State),
            Deferred = DeferredSnapshot(r.Deferred),
            r.InitialHorizonEvents,
        };
    object ReadinessSnapshot(ResearchReadinessBreakdown r) => r;
    object CommandSnapshot(AdaptiveResearchCommandResult r) => r;
    object EventsSnapshot(IReadOnlyList<AdaptiveResearchRuntimeEvent> e) => e.ToArray();

    var profiles = new List<object>();
    foreach (var profileId in authority.StartingProfiles.ReferenceProfileIds)
    {
        var civ = "fixture:profile:" + profileId;
        var context = "fixture:context:" + profileId;
        var before = Freeze(
            new
            {
                CivilizationId = civ,
                ProfileId = profileId,
                Context = context,
                ActivityYear = 2042.5,
            }
        );
        AdaptiveResearchStartingCompositionResult? result = null;
        Exception? error = null;
        try
        {
            result = authority.ComposeReferenceProfile(civ, profileId, context, 2042.5);
        }
        catch (Exception e)
        {
            error = e;
        }
        var frozen = result is null ? (JsonElement?)null : Freeze(CompositionSnapshot(result));
        var frozenError = error is null
            ? (JsonElement?)null
            : Freeze(new { Type = error.GetType().Name, error.Message });
        profiles.Add(
            new
            {
                Name = "compose-" + profileId,
                Input = before,
                Result = frozen,
                Error = frozenError,
            }
        );
    }

    var sequence = new List<object>();
    var composition = authority.ComposeReferenceProfile(
        "fixture:sequence",
        "reference_humanlike_solar_2050",
        "fixture:primary",
        2050
    );
    var state = composition.State;
    var civilizationTrait = authority
        .Applicability.Traits.Values.First(t =>
            t.Scope == ResearchApplicabilityTraitScope.Civilization
        )
        .Id;
    var populationTraits = authority
        .Applicability.Traits.Values.Where(t =>
            t.Scope == ResearchApplicabilityTraitScope.PopulationOrSpecies
        )
        .Take(2)
        .Select(t => t.Id)
        .ToArray();
    var contextualCapability = authority.Catalog.Capabilities.Values.First(c =>
        c.Scope != ResearchCapabilityScope.Civilization
    );
    void Step(string name, object input, Func<object?> invoke)
    {
        var frozenInput = Freeze(input);
        var before = Freeze(StateSnapshot(state));
        object? result = null;
        Exception? error = null;
        try
        {
            result = invoke();
        }
        catch (Exception e)
        {
            error = e;
        }
        var frozenResult = result is null ? (JsonElement?)null : Freeze(result);
        var frozenError = error is null
            ? (JsonElement?)null
            : Freeze(new { Type = error.GetType().Name, error.Message });
        var after = Freeze(StateSnapshot(state));
        sequence.Add(
            new
            {
                Name = name,
                Input = frozenInput,
                Before = before,
                Result = frozenResult,
                Error = frozenError,
                After = after,
            }
        );
    }
    Step("build-view", new { Op = "BuildView" }, () => authority.BuildView(state));
    Step(
        "readiness-fusion",
        new
        {
            Op = "Readiness",
            NodeId = "fusion_power",
            Stage = (int)ResearchMaturity.Experimental,
            Labs = 4.0,
            Context = "fixture:primary",
        },
        () =>
            ReadinessSnapshot(
                authority.GetProjectReadiness(
                    state,
                    "fusion_power",
                    ResearchMaturity.Experimental,
                    4,
                    "fixture:primary"
                )
            )
    );
    Step(
        "start-fusion",
        new
        {
            Op = "Start",
            NodeId = "fusion_power",
            Labs = 4.0,
            Context = "fixture:primary",
        },
        () =>
            CommandSnapshot(
                authority.StartDirectedResearch(state, "fusion_power", 4, "fixture:primary")
            )
    );
    Step(
        "pause-fusion",
        new { Op = "Pause", NodeId = "fusion_power" },
        () => CommandSnapshot(authority.PauseDirectedResearch(state, "fusion_power"))
    );
    Step(
        "resume-fusion",
        new
        {
            Op = "Resume",
            NodeId = "fusion_power",
            Labs = 4.0,
        },
        () => CommandSnapshot(authority.ResumeDirectedResearch(state, "fusion_power", 4))
    );
    Step(
        "reallocate-fusion",
        new
        {
            Op = "Reallocate",
            NodeId = "fusion_power",
            Labs = 6.0,
        },
        () => CommandSnapshot(authority.ReallocateResearchLabs(state, "fusion_power", 6))
    );
    Step(
        "pressure",
        new
        {
            Op = "Pressure",
            Id = "energy_shortage",
            Value = 37.5,
            Context = "fixture:primary",
        },
        () =>
            EventsSnapshot(authority.SetPressure(state, "energy_shortage", 37.5, "fixture:primary"))
    );
    Step(
        "trait",
        new { Op = "Trait", Id = civilizationTrait },
        () => EventsSnapshot(authority.AddCivilizationTrait(state, civilizationTrait))
    );
    Step(
        "context-traits",
        new
        {
            Op = "ContextTraits",
            Id = "fixture:primary",
            Traits = populationTraits,
        },
        () =>
            EventsSnapshot(
                authority.SetApplicabilityContextTraits(state, "fixture:primary", populationTraits)
            )
    );
    Step(
        "capability",
        new
        {
            Op = "Capability",
            Id = contextualCapability.Id,
            Context = "fixture:primary",
        },
        () =>
            EventsSnapshot(
                authority.AddCapability(state, contextualCapability.Id, "fixture:primary")
            )
    );
    Step(
        "review-candidates",
        new
        {
            Op = "Review",
            Ids = new[] { "fusion_power", "high_temp_superconductors" },
            Context = "fixture:primary",
        },
        () =>
            EventsSnapshot(
                authority.ReviewBasicScienceCandidates(
                    state,
                    new[] { "fusion_power", "high_temp_superconductors" },
                    "fixture:primary"
                )
            )
    );
    Step(
        "advance-epsilon",
        new
        {
            Op = "Advance",
            Elapsed = .0000001,
            Current = 2050.0000001,
        },
        () => EventsSnapshot(authority.AdvanceProjects(state, .0000001, 2050.0000001))
    );
    Step(
        "advance-segmented",
        new
        {
            Op = "Advance",
            Elapsed = 80.0,
            Current = 2130.0,
        },
        () => EventsSnapshot(authority.AdvanceProjects(state, 80, 2130))
    );
    Step(
        "advance-negative",
        new
        {
            Op = "Advance",
            Elapsed = -1.0,
            Current = 2130.0,
        },
        () => EventsSnapshot(authority.AdvanceProjects(state, -1, 2130))
    );

    // Capacity loss after a live allocation: remove every seeded institution through the public facade.
    var cap = authority.ComposeReferenceProfile(
        "fixture:capacity",
        "reference_humanlike_solar_2050",
        "fixture:primary",
        2050
    );
    state = cap.State;
    Step(
        "capacity-start",
        new
        {
            Op = "Start",
            NodeId = "fusion_power",
            Labs = 4.0,
            Context = "fixture:primary",
            Setup = new
            {
                Kind = "Compose",
                ProfileId = "reference_humanlike_solar_2050",
                CivilizationId = "fixture:capacity",
                Context = "fixture:primary",
                ActivityYear = 2050.0,
            },
        },
        () =>
            CommandSnapshot(
                authority.StartDirectedResearch(state, "fusion_power", 4, "fixture:primary")
            )
    );
    foreach (var institution in state.Expertise.Institutions.Values.ToArray())
    {
        var id = institution.InstitutionInstanceId;
        var archetype = institution.InstitutionArchetypeId;
        Step(
            "capacity-remove-" + id,
            new
            {
                Op = "Institution",
                InstanceId = id,
                ArchetypeId = archetype,
                Total = 0,
                Active = 0,
                Context = (string?)null,
            },
            () =>
            {
                authority.SetResearchInstitution(state, id, archetype, 0, 0, null);
                return true;
            }
        );
    }

    // Controlled active project demonstrates duplicate evidence replacement plus mandatory all-project recalculation.
    state = authority.CreateCivilizationState("fixture:evidence");
    var evidenceNode = authority.Catalog.Nodes.Values.First(n =>
        n.Applicability.EvidenceTypes.Count > 0
    );
    var evidenceType = evidenceNode.Applicability.EvidenceTypes[0];
    Core(state, "SetTotalEffectiveResearchLabs", 10.0);
    Core(
        state,
        "SetNodeState",
        new ResearchNodeRuntimeState(evidenceNode.Id, ResearchMaturity.Investigable, null, 0, 0, 0)
    );
    Core(
        state,
        "SetProject",
        new ResearchProjectRuntimeState(
            evidenceNode.Id,
            ResearchMaturity.Experimental,
            null,
            2,
            0.1,
            false,
            null,
            0,
            0,
            0
        )
    );
    var evidenceSetup = new
    {
        Kind = "ControlledEvidence",
        CivilizationId = "fixture:evidence",
        NodeId = evidenceNode.Id,
        EvidenceTypeId = evidenceType,
        TotalLabs = 10.0,
        ProjectStage = (int)ResearchMaturity.Experimental,
        AssignedLabs = 2.0,
        Readiness = .1,
    };
    Step(
        "evidence-first",
        new
        {
            Op = "Evidence",
            InstanceId = "duplicate",
            TypeId = evidenceType,
            Provenance = "fixture:first",
            Quality = .2,
            Confidence = .4,
            Context = (string?)null,
            Setup = evidenceSetup,
        },
        () =>
            EventsSnapshot(
                authority.AddEvidence(
                    state,
                    "duplicate",
                    evidenceType,
                    "fixture:first",
                    .2,
                    .4,
                    null
                )
            )
    );
    Core(
        state,
        "SetProject",
        state.ActiveProjects[evidenceNode.Id] with
        {
            ReadinessEfficiency = .432,
        }
    );
    Step(
        "evidence-duplicate-recalculates",
        new
        {
            Op = "Evidence",
            InstanceId = "duplicate",
            TypeId = evidenceType,
            Provenance = "fixture:replacement",
            Quality = .9,
            Confidence = .8,
            Context = (string?)null,
            Setup = new
            {
                Kind = "ExistingProjectReadiness",
                NodeId = evidenceNode.Id,
                Readiness = .432,
            },
        },
        () =>
            EventsSnapshot(
                authority.AddEvidence(
                    state,
                    "duplicate",
                    evidenceType,
                    "fixture:replacement",
                    .9,
                    .8,
                    null
                )
            )
    );
    var corrupt = state.ActiveProjects[evidenceNode.Id] with { ReadinessEfficiency = .123 };
    Core(state, "SetProject", corrupt);
    Step(
        "atrophy-zero-recalculates",
        new
        {
            Op = "Atrophy",
            Current = 2100.0,
            Elapsed = 0.0,
            Setup = new
            {
                Kind = "ExistingProjectReadiness",
                NodeId = evidenceNode.Id,
                Readiness = .123,
            },
        },
        () =>
        {
            authority.ApplyCompetenceAtrophy(state, 2100, 0);
            return true;
        }
    );
    corrupt = state.ActiveProjects[evidenceNode.Id] with { ReadinessEfficiency = .234 };
    Core(state, "SetProject", corrupt);
    Step(
        "atrophy-nan-recalculates",
        new
        {
            Op = "Atrophy",
            Current = 2100.0,
            Elapsed = double.NaN,
            Setup = new
            {
                Kind = "ExistingProjectReadiness",
                NodeId = evidenceNode.Id,
                Readiness = .234,
            },
        },
        () =>
        {
            authority.ApplyCompetenceAtrophy(state, 2100, double.NaN);
            return true;
        }
    );
    Step(
        "atrophy-valid",
        new
        {
            Op = "Atrophy",
            Current = 2100.0,
            Elapsed = 25.0,
        },
        () =>
        {
            authority.ApplyCompetenceAtrophy(state, 2100, 25);
            return true;
        }
    );
    var tacitType = authority.ExpertiseCatalog.TacitAssetTypes.Values.First();
    Step(
        "set-tacit",
        new
        {
            Op = "Tacit",
            AssetId = "fixture:tacit",
            TypeId = tacitType.Id,
            Scope = (int)ResearchTacitScopeKind.TechnologyNode,
            ScopeRef = evidenceNode.Id,
            Assimilation = (int)ResearchTacitAssimilationStage.Interpreted,
            Depth = 45.0,
            Availability = .8,
            Translation = .7,
            Training = .6,
            Provenance = "fixture:tacit",
            Context = (string?)null,
        },
        () =>
        {
            authority.SetTacitAsset(
                state,
                "fixture:tacit",
                tacitType.Id,
                ResearchTacitScopeKind.TechnologyNode,
                evidenceNode.Id,
                ResearchTacitAssimilationStage.Interpreted,
                45,
                .8,
                .7,
                .6,
                "fixture:tacit",
                null
            );
            return true;
        }
    );
    Step(
        "start-invalid-labs-before-node",
        new
        {
            Op = "Start",
            NodeId = "missing-node",
            Labs = double.NaN,
            Context = (string?)null,
        },
        () =>
            CommandSnapshot(
                authority.StartDirectedResearch(state, "missing-node", double.NaN, null)
            )
    );
    Step(
        "resume-invalid-labs-before-missing",
        new
        {
            Op = "Resume",
            NodeId = "missing-node",
            Labs = double.NaN,
        },
        () => CommandSnapshot(authority.ResumeDirectedResearch(state, "missing-node", double.NaN))
    );

    var hypothesisNode = authority.Catalog.Nodes.Values.First(n => n.IsHypothesis);
    var runningNode = authority.Catalog.Nodes.Values.First(n =>
        !n.IsHypothesis && n.Id != hypothesisNode.Id
    );
    state = authority.CreateCivilizationState("fixture:paused-hypothesis");
    Core(state, "SetTotalEffectiveResearchLabs", 10.0);
    Core(
        state,
        "SetNodeState",
        new ResearchNodeRuntimeState(
            hypothesisNode.Id,
            ResearchMaturity.Investigable,
            null,
            0,
            0,
            0
        )
    );
    Core(
        state,
        "SetNodeState",
        new ResearchNodeRuntimeState(runningNode.Id, ResearchMaturity.Investigable, null, 0, 0, 0)
    );
    Core(
        state,
        "SetProject",
        new ResearchProjectRuntimeState(
            hypothesisNode.Id,
            ResearchMaturity.Experimental,
            null,
            1,
            .5,
            true,
            "hypothesis_resolution_required",
            0,
            0,
            0
        )
    );
    var runningWork = authority.ProgressPolicy.GetStageWork(
        runningNode,
        ResearchMaturity.Experimental
    );
    Core(
        state,
        "SetProject",
        new ResearchProjectRuntimeState(
            runningNode.Id,
            ResearchMaturity.Experimental,
            null,
            1,
            1,
            false,
            null,
            runningWork - .00001,
            0,
            0
        )
    );
    var hypothesisSetup = new
    {
        Kind = "PausedHypothesis",
        CivilizationId = "fixture:paused-hypothesis",
        HypothesisNodeId = hypothesisNode.Id,
        RunningNodeId = runningNode.Id,
        TotalLabs = 10.0,
        RunningWork = runningWork,
    };
    Step(
        "paused-hypothesis-practice-quirk",
        new
        {
            Op = "Advance",
            Elapsed = .01,
            Current = 2050.01,
            Setup = hypothesisSetup,
        },
        () => EventsSnapshot(authority.AdvanceProjects(state, .01, 2050.01))
    );
    Step(
        "resolve-paused-hypothesis",
        new
        {
            Op = "Resolve",
            NodeId = hypothesisNode.Id,
            Supported = true,
        },
        () => CommandSnapshot(authority.ResolveHypothesis(state, hypothesisNode.Id, true))
    );

    state = authority.CreateCivilizationState("fixture:partial-evidence");
    Core(state, "SetTotalEffectiveResearchLabs", 10.0);
    Core(
        state,
        "SetProject",
        new ResearchProjectRuntimeState(
            "missing-active-node",
            ResearchMaturity.Experimental,
            null,
            1,
            .5,
            false,
            null,
            0,
            0,
            0
        )
    );
    var partialSetup = new
    {
        Kind = "PartialEvidenceFailure",
        CivilizationId = "fixture:partial-evidence",
        ProjectNodeId = "missing-active-node",
        TotalLabs = 10.0,
    };
    Step(
        "evidence-partial-error-retains-mutation",
        new
        {
            Op = "Evidence",
            InstanceId = "partial",
            TypeId = evidenceType,
            Provenance = "fixture:partial",
            Quality = .7,
            Confidence = .8,
            Context = (string?)null,
            Setup = partialSetup,
        },
        () =>
            EventsSnapshot(
                authority.AddEvidence(
                    state,
                    "partial",
                    evidenceType,
                    "fixture:partial",
                    .7,
                    .8,
                    null
                )
            )
    );

    var afterFingerprint = Fingerprint(root);
    if (!string.Equals(beforeFingerprint, afterFingerprint, StringComparison.Ordinal))
        throw new InvalidOperationException("Authority replay changed canonical research data.");
    var output = new
    {
        Generator = "actual C# AdaptiveResearchAuthority",
        CanonicalFingerprint = beforeFingerprint,
        AfterFingerprint = afterFingerprint,
        Profiles = profiles,
        Sequence = sequence,
        EvidenceControls = new { NodeId = evidenceNode.Id, EvidenceTypeId = evidenceType },
        HypothesisControls = new
        {
            HypothesisNodeId = hypothesisNode.Id,
            RunningNodeId = runningNode.Id,
        },
    };
    File.WriteAllText(
        Path.GetFullPath(args[1]),
        JsonSerializer.Serialize(output, options) + Environment.NewLine
    );
    return 0;
}
catch (Exception e)
{
    Console.Error.WriteLine(
        $"ResearchAuthorityOracle failed: {e}\nCWD: {Environment.CurrentDirectory}\nResearch root: {Path.GetFullPath(args[0])}\nFixture: {Path.GetFullPath(args[1])}"
    );
    return 1;
}
