using System.Globalization;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var diagnosticRoot = args.Length > 0 ? args[0] : "<missing>";
var diagnosticFixture = args.Length > 1 ? args[1] : "<missing>";
try {
    if (args.Length != 2) throw new ArgumentException("Expected research root and fixture path.");
    var root = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    var options = new JsonSerializerOptions { NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
    JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
    string Fingerprint(string? directory = null) {
        directory ??= root;
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in Directory.GetFiles(directory, "*.json").OrderBy(Path.GetFileName, StringComparer.Ordinal)) {
            hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
            hash.AppendData(File.ReadAllBytes(path));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }
    object AgendaState(AdaptiveResearchAgendaState state) => new {
        state.Revision, state.Orientations, state.LastMajorReviewYear, state.PolicyProvenance,
        DomainPriorities = state.DomainPriorities.ToArray(), FieldPriorities = state.FieldPriorities.ToArray(),
        ProblemPriorities = state.ProblemPriorities.ToArray(), CapabilityPriorities = state.CapabilityPriorities.ToArray(),
        CultureAxes = state.CultureAxes.ToArray(),
    };
    var beforeLoad = Fingerprint();
    var authority = AdaptiveResearchAuthority.LoadFromDirectory(root);
    var catalog = AdaptiveResearchAgendaCatalog.LoadFromDirectory(root, authority.Catalog, authority.ExpertiseCatalog);
    var runtime = new AdaptiveResearchAgendaRuntime(authority, catalog);
    var afterLoad = Fingerprint();
    if (beforeLoad != afterLoad) throw new InvalidOperationException("Agenda loading changed canonical inputs.");
    var rows = new List<object>();
    rows.Add(new { Name="catalog-canonical", BeforeFingerprint=beforeLoad, AfterFingerprint=afterLoad,
        Result=Freeze(new { Priorities=catalog.Priorities.Values.ToArray(), CultureAxes=catalog.CultureAxes.Values.ToArray(),
            catalog.UtilityComponentIds, catalog.ShortlistBound, catalog.RuntimePolicy }) });
    var composition = authority.ComposeReferenceProfile("fixture:agenda", "reference_humanlike_solar_2050", "fixture:context", 2050);
    var state = composition.State;
    void Step(string name, object input, Func<object?> call) {
        var ownedInput = Freeze(input);
        var before = Fingerprint();
        object? result = null; Exception? error = null;
        try { result = call(); } catch (Exception caught) { error = caught; }
        var frozenResult = result is null ? (JsonElement?)null : Freeze(result);
        var frozenError = error is null ? (JsonElement?)null : Freeze(new { Type=error.GetType().Name, error.Message });
        var agenda = Freeze(AgendaState(runtime.GetState(state)));
        var after = Fingerprint();
        rows.Add(new { Name=name, Input=ownedInput, BeforeFingerprint=before, AfterFingerprint=after,
            Result=frozenResult, Error=frozenError, CoreRevision=state.Revision,
            state.MaterializedViewRevision, ExpertiseRevision=state.Expertise.Revision, Agenda=agenda });
    }
    Step("state-default", new { Op="GetState" }, () => AgendaState(runtime.GetState(state)));
    IReadOnlyList<ResearchVisibleProjectCandidate>? first = null;
    Step("shortlist-first", new { Op="BuildVisibleShortlist" }, () => first = runtime.BuildVisibleShortlist(state));
    Step("shortlist-cache-hit", new { Op="BuildVisibleShortlist" }, () => {
        var value=runtime.BuildVisibleShortlist(state); return new { SameReference=ReferenceEquals(first,value), Candidates=value };
    });
    var domain = authority.Catalog.Nodes.Values.First().DomainId;
    Step("priority-default-noop", new { Op="SetDomainPriority", domain, Priority="routine" }, () => { runtime.SetDomainPriority(state,domain,"routine"); return true; });
    Step("shortlist-after-priority-noop", new { Op="BuildVisibleShortlist" }, () => runtime.BuildVisibleShortlist(state));
    Step("priority-important", new { Op="SetDomainPriority", domain, Priority="important" }, () => { runtime.SetDomainPriority(state,domain,"important"); return true; });
    Step("shortlist-after-agenda-revision", new { Op="BuildVisibleShortlist" }, () => runtime.BuildVisibleShortlist(state));
    var pressure = authority.Catalog.PressureIds.First();
    Step("core-revision-pressure", new { Op="SetPressure", pressure, Value=17.5 }, () => authority.SetPressure(state,pressure,17.5));
    Step("shortlist-after-core-revision", new { Op="BuildVisibleShortlist" }, () => runtime.BuildVisibleShortlist(state));

    var expertiseMethod = typeof(AdaptiveResearchExpertiseState).GetMethod("SetField", BindingFlags.Instance|BindingFlags.NonPublic)
        ?? throw new MissingMethodException("AdaptiveResearchExpertiseState.SetField");
    var field = authority.ExpertiseCatalog.Fields.Keys.First();
    var existing = state.Expertise.GetField(field);
    try { expertiseMethod.Invoke(state.Expertise, new object?[] { existing with { Current = existing.Current with { Theoretical = existing.Current.Theoretical + .25 } } }); }
    catch (TargetInvocationException caught) when (caught.InnerException is not null) { ExceptionDispatchInfo.Capture(caught.InnerException).Throw(); }
    Step("shortlist-after-expertise-revision", new { Op="BuildVisibleShortlist", Setup="expertise-writer" }, () => runtime.BuildVisibleShortlist(state));

    var axis = catalog.CultureAxes.Keys.First();
    Step("culture-noop", new { Op="SetCulture", axis, Value=50.0 }, () => { runtime.SetScientificCultureAxis(state,axis,50); return true; });
    Step("culture-change", new { Op="SetCulture", axis, Value=62.5 }, () => { runtime.SetScientificCultureAxis(state,axis,62.5); return true; });
    Step("orientations-change", new { Op="SetOrientations" }, () => { runtime.SetOrientations(state,new(35,65,75,45)); return true; });
    var fieldPriority = authority.ExpertiseCatalog.Fields.Keys.First();
    var capability = authority.Catalog.Capabilities.Keys.First();
    Step("field-priority", new { Op="SetFieldPriority", fieldPriority }, () => { runtime.SetFieldPriority(state,fieldPriority,"strategic"); return true; });
    Step("problem-priority", new { Op="SetProblemPriority", pressure }, () => { runtime.SetProblemPriority(state,pressure,"critical"); return true; });
    Step("capability-priority", new { Op="SetCapabilityPriority", capability }, () => { runtime.SetCapabilityPriority(state,capability,"deprioritized"); return true; });

    void Assess(string name, double adequacy, double challenge, double confidence) =>
        Step(name, new { Op="Evaluate", domain, adequacy, challenge, confidence }, () => runtime.EvaluatePerceivedAdequacy(state,new(domain,adequacy,challenge,confidence)));
    Assess("adequacy-low-confidence",50,100,.1);
    runtime.SetScientificCultureAxis(state,"threat_sensitivity",100);
    Assess("adequacy-critical",20,90,1);
    Assess("adequacy-strategic",20,65,1);
    Assess("adequacy-important",20,40,1);
    runtime.SetScientificCultureAxis(state,"threat_sensitivity",0);
    runtime.SetScientificCultureAxis(state,"complacency_tendency",100);
    runtime.SetScientificCultureAxis(state,"institutional_conservatism",100);
    Assess("adequacy-deprioritize",100,0,1);
    Assess("adequacy-unchanged",40,0,1);
    var recommendation = runtime.EvaluatePerceivedAdequacy(state,new(domain,100,0,1));
    Step("apply-recommendation", new { Op="ApplyRecommendation", recommendation, Year=2125.5, Provenance="fixture" }, () => { runtime.ApplyRecommendation(state,recommendation,2125.5,"fixture"); return true; });

    var shortlist = runtime.BuildVisibleShortlist(state);
    var allowed = shortlist.FirstOrDefault(value => value.CanStart);
    if (allowed is not null) {
        var command = authority.StartDirectedResearch(state,allowed.NodeId,allowed.RequestedEffectiveLabs);
        if (!command.Accepted) throw new InvalidOperationException(command.Message);
        Step("shortlist-active-excluded", new { Op="BuildVisibleShortlist", Excluded=allowed.NodeId }, () => runtime.BuildVisibleShortlist(state));
        authority.PauseDirectedResearch(state,allowed.NodeId);
        Step("shortlist-paused-excluded", new { Op="BuildVisibleShortlist", Excluded=allowed.NodeId }, () => runtime.BuildVisibleShortlist(state));
    }
    Step("error-domain", new { Op="SetDomainPriority", Id="unknown" }, () => { runtime.SetDomainPriority(state,"unknown","routine"); return true; });
    Step("error-field", new { Op="SetFieldPriority", Id="unknown" }, () => { runtime.SetFieldPriority(state,"unknown","routine"); return true; });
    Step("error-problem", new { Op="SetProblemPriority", Id="unknown" }, () => { runtime.SetProblemPriority(state,"unknown","routine"); return true; });
    Step("error-capability", new { Op="SetCapabilityPriority", Id="unknown" }, () => { runtime.SetCapabilityPriority(state,"unknown","routine"); return true; });
    Step("error-priority", new { Op="SetDomainPriority", domain, Id="unknown" }, () => { runtime.SetDomainPriority(state,domain,"unknown"); return true; });
    var freshErrorState=authority.ComposeReferenceProfile("fixture:error-state","reference_humanlike_solar_2050","fixture:error-state",2050).State;
    var stateTableField=typeof(AdaptiveResearchAgendaRuntime).GetField("_states",BindingFlags.Instance|BindingFlags.NonPublic)
        ?? throw new MissingFieldException("AdaptiveResearchAgendaRuntime._states");
    var stateTable=(ConditionalWeakTable<AdaptiveResearchCivilizationState,AdaptiveResearchAgendaState>?)stateTableField.GetValue(runtime)
        ?? throw new InvalidOperationException("Agenda state table was unavailable.");
    var ownedFreshInput=Freeze(new { Op="SetDomainPriority", domain, Id="unknown", State="fresh" });
    var freshBefore=Fingerprint(); Exception? freshError=null;
    Func<object> freshCall=()=>{runtime.SetDomainPriority(freshErrorState,domain,"unknown");return true;};
    try { _=freshCall(); } catch(Exception caught) { freshError=caught; }
    var freshCreated=stateTable.TryGetValue(freshErrorState,out _);
    var freshAfter=Fingerprint();
    rows.Add(new { Name="error-priority-fresh-state", Input=ownedFreshInput, BeforeFingerprint=freshBefore, AfterFingerprint=freshAfter,
        Result=Freeze(new { StateCreated=freshCreated }), Error=freshError is null ? (JsonElement?)null : Freeze(new { Type=freshError.GetType().Name,freshError.Message }),
        CoreRevision=state.Revision, state.MaterializedViewRevision, ExpertiseRevision=state.Expertise.Revision, Agenda=Freeze(AgendaState(runtime.GetState(state))) });
    Step("error-culture-axis", new { Op="SetCulture", Id="unknown" }, () => { runtime.SetScientificCultureAxis(state,"unknown",50); return true; });
    Step("error-culture-negative", new { Op="SetCulture", axis, Value=-1.0 }, () => { runtime.SetScientificCultureAxis(state,axis,-1); return true; });
    foreach(var numeric in new[] { ("error-culture-1e6",1e6), ("error-culture-1e16",1e16),
        ("error-culture-1e17",1e17), ("error-culture-minus-1e4",-1e-4),
        ("error-culture-minus-1e5",-1e-5) })
        Step(numeric.Item1, new { Op="SetCulture", axis, numeric.Item2 }, () => { runtime.SetScientificCultureAxis(state,axis,numeric.Item2); return true; });
    Step("error-orientation-nan", new { Op="SetOrientations", Value=double.NaN }, () => { runtime.SetOrientations(state,new(double.NaN,50,50,50)); return true; });
    Step("error-assessment-confidence", new { Op="Evaluate", Confidence=double.NaN }, () => runtime.EvaluatePerceivedAdequacy(state,new(domain,50,50,double.NaN)));

    Step("identity-two-states", new { Op="SeparateStateIdentity" }, () => {
        var left = authority.ComposeReferenceProfile("fixture:same", "reference_humanlike_solar_2050", "fixture:left", 2050).State;
        var right = authority.ComposeReferenceProfile("fixture:same", "reference_humanlike_solar_2050", "fixture:right", 2050).State;
        runtime.SetDomainPriority(left,domain,"critical");
        return new { Left=runtime.GetState(left).GetDomainPriority(domain,catalog.RuntimePolicy.DefaultPriorityId),
            Right=runtime.GetState(right).GetDomainPriority(domain,catalog.RuntimePolicy.DefaultPriorityId) };
    });
    Step("identity-two-runtimes", new { Op="SeparateRuntimeIdentity" }, () => {
        var isolated = new AdaptiveResearchAgendaRuntime(authority,catalog);
        isolated.SetDomainPriority(state,domain,"critical");
        return new { Primary=runtime.GetState(state).GetDomainPriority(domain,catalog.RuntimePolicy.DefaultPriorityId),
            Isolated=isolated.GetState(state).GetDomainPriority(domain,catalog.RuntimePolicy.DefaultPriorityId) };
    });
    var sparseDomains=authority.Catalog.Nodes.Values.Select(value=>value.DomainId).Distinct(StringComparer.Ordinal).Take(4).ToArray();
    if(sparseDomains.Length!=4) throw new InvalidOperationException("Agenda sparse-order fixture needs four domains.");
    Step("sparse-priority-seed", new { Op="SparseSeed", sparseDomains }, () => {
        runtime.SetDomainPriority(state,sparseDomains[0],"important");
        runtime.SetDomainPriority(state,sparseDomains[1],"strategic");
        runtime.SetDomainPriority(state,sparseDomains[2],"critical");
        return runtime.GetState(state).DomainPriorities.ToArray();
    });
    Step("sparse-priority-remove-two", new { Op="SparseRemove", sparseDomains }, () => {
        runtime.SetDomainPriority(state,sparseDomains[0],"routine");
        runtime.SetDomainPriority(state,sparseDomains[1],"routine");
        return runtime.GetState(state).DomainPriorities.ToArray();
    });
    Step("sparse-priority-reinsert-lifo", new { Op="SparseReinsert", sparseDomains }, () => {
        runtime.SetDomainPriority(state,sparseDomains[3],"important");
        runtime.SetDomainPriority(state,sparseDomains[0],"strategic");
        return runtime.GetState(state).DomainPriorities.ToArray();
    });

    string ChangeJson(string text, Action<JsonObject> change) {
        var value=JsonNode.Parse(text)?.AsObject() ?? throw new InvalidDataException("Expected JSON object fixture.");
        change(value);
        return value.ToJsonString();
    }
    var malformed = new (string Name, string File, Func<string,string?> Change)[] {
        ("load-missing-agenda", "research_agenda_model.json", _ => null),
        ("load-malformed-agenda-json", "research_agenda_model.json", _ => "{"),
        ("load-agenda-catalog-mismatch", "research_agenda_model.json", text => text.Replace(authority.Catalog.Metadata.CatalogId, "wrong.catalog", StringComparison.Ordinal)),
        ("load-duplicate-priority", "research_agenda_model.json", text => text.Replace("\"id\":\"routine\"", "\"id\":\"deprioritized\"", StringComparison.Ordinal)),
        ("load-priority-rank-gap", "research_agenda_model.json", text => text.Replace("\"rank\":4", "\"rank\":3", StringComparison.Ordinal)),
        ("load-duplicate-culture-axis", "scientific_culture_model.json", text => text.Replace("\"id\":\"risk_tolerance\"", "\"id\":\"curiosity\"", StringComparison.Ordinal)),
        ("load-culture-range", "scientific_culture_model.json", text => text.Replace("\"range\":[0,100]", "\"range\":[1,100]", StringComparison.Ordinal)),
        ("load-blank-utility-component", "research_ai_planning_contract.json", text => text.Replace("\"id\":\"recognized_need\"", "\"id\":\"   \"", StringComparison.Ordinal)),
        ("load-shortlist-zero", "research_ai_planning_contract.json", text => text.Replace("\"bounded_candidate_count\":12", "\"bounded_candidate_count\":0", StringComparison.Ordinal)),
        ("load-default-priority-unknown", "research_agenda_runtime_policy.json", text => text.Replace("\"priority_level\": \"routine\"", "\"priority_level\": \"unknown\"", StringComparison.Ordinal)),
        ("load-default-orientation-negative", "research_agenda_runtime_policy.json", text => text.Replace("\"basic_vs_applied_orientation\": 50", "\"basic_vs_applied_orientation\": -1", StringComparison.Ordinal)),
        ("load-priorities-wrong-kind", "research_agenda_model.json", text => ChangeJson(text, value => value["priority_levels"]=new JsonObject())),
        ("load-culture-axes-null", "scientific_culture_model.json", text => ChangeJson(text, value => value["axes"]=null)),
        ("load-priority-scores-wrong-kind", "research_agenda_runtime_policy.json", text => ChangeJson(text, value => value["priority_scores"]=new JsonArray())),
    };
    var relevantFiles = new[] { "research_agenda_model.json", "scientific_culture_model.json", "research_ai_planning_contract.json", "research_agenda_runtime_policy.json" };
    var scratchParent = Path.Combine(Path.GetTempPath(), "stellar-agenda-056-owned");
    Directory.CreateDirectory(scratchParent);
    foreach (var test in malformed) {
        var scratch = Path.GetFullPath(Path.Combine(scratchParent, Guid.NewGuid().ToString("N")));
        var canonicalParent = Path.GetFullPath(scratchParent).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (!scratch.StartsWith(canonicalParent, StringComparison.OrdinalIgnoreCase) || Directory.Exists(scratch))
            throw new InvalidOperationException("Refused unsafe Agenda scratch path.");
        Directory.CreateDirectory(scratch);
        try {
            foreach (var file in relevantFiles) File.Copy(Path.Combine(root,file),Path.Combine(scratch,file));
            var changedPath = Path.Combine(scratch,test.File);
            var changed = test.Change(File.ReadAllText(changedPath));
            if (changed is null) File.Delete(changedPath); else File.WriteAllText(changedPath,changed,new UTF8Encoding(false));
            var changedFiles = relevantFiles.Where(file => !File.Exists(Path.Combine(root,file)) || !File.Exists(Path.Combine(scratch,file)) ||
                !File.ReadAllBytes(Path.Combine(root,file)).SequenceEqual(File.ReadAllBytes(Path.Combine(scratch,file))))
                .Select(file => new { RelativePath=file, Deleted=!File.Exists(Path.Combine(scratch,file)),
                    ContentBase64=File.Exists(Path.Combine(scratch,file)) ? Convert.ToBase64String(File.ReadAllBytes(Path.Combine(scratch,file))) : null }).ToArray();
            var before=Fingerprint(scratch); object? value=null; Exception? error=null;
            Func<object> call=()=>AdaptiveResearchAgendaCatalog.LoadFromDirectory(scratch,authority.Catalog,authority.ExpertiseCatalog);
            try { value=call(); } catch(Exception caught) { error=caught; }
            var after=Fingerprint(scratch);
            if(before!=after) throw new InvalidOperationException($"Production call '{test.Name}' changed malformed inputs.");
            rows.Add(new { Name=test.Name, Phase="Load", ChangedFiles=changedFiles, BeforeFingerprint=before, AfterFingerprint=after,
                Result=value is null ? (JsonElement?)null : Freeze(value), Error=error is null ? (JsonElement?)null : Freeze(new { Type=error.GetType().Name,error.Message }) });
        } finally {
            var resolved=Path.GetFullPath(scratch);
            if(Directory.Exists(resolved) && string.Equals(Path.GetDirectoryName(resolved),Path.GetFullPath(scratchParent),StringComparison.OrdinalIgnoreCase))
                Directory.Delete(resolved,true);
        }
    }

    var fixture = new { Generator="actual C# AdaptiveResearchAgenda", CanonicalFingerprint=beforeLoad, Rows=rows };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(fixture,options)+"\n");
    Console.WriteLine($"generated {rows.Count} actual-source rows");
    Console.WriteLine($"canonical fingerprint {beforeLoad}");
    return 0;
} catch (Exception error) {
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd={Directory.GetCurrentDirectory()}");
    Console.Error.WriteLine($"research-root={diagnosticRoot}");
    Console.Error.WriteLine($"fixture={diagnosticFixture}");
    return 1;
}
