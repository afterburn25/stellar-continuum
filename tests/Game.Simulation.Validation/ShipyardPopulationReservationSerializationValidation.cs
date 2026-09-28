using System.Runtime.CompilerServices;
using Game.Persistence;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Validation;

internal static class ShipyardPopulationReservationSerializationValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateV8AndV9RejectPopulationLosingShipyardState();
        ValidateZeroPopulationOverflowCanStillBeBoundedSafely();
        Console.WriteLine("PASS: v8/v9 persistence refuses population loss while harmless queue overflow remains bounded");
    }

    private static void ValidateV8AndV9RejectPopulationLosingShipyardState()
    {
        WithTemporaryDirectory(directory =>
        {
            ExpectRejectedByBothPersistencePaths(
                directory,
                "invalid-active",
                (state, speciesId, colonyDesign) =>
                {
                    state.ActiveDesignId = "unknown_population_transport";
                    state.ActiveBuildProgress = 10.0;
                    state.ReservedPopulationMillions = colonyDesign.PopulationCostMillions;
                    state.ReservedPopulationSpeciesId = speciesId;
                });

            ExpectRejectedByBothPersistencePaths(
                directory,
                "invalid-queued",
                (state, speciesId, colonyDesign) =>
                {
                    state.ActiveDesignId = null;
                    state.ActiveBuildProgress = 0.0;
                    state.ReservedPopulationMillions = 0.0;
                    state.ReservedPopulationSpeciesId = null;
                    state.QueuedBuilds.Add(new ShipBuildOrderState
                    {
                        DesignId = "unknown_population_transport",
                        ReservedPopulationMillions = colonyDesign.PopulationCostMillions,
                        ReservedPopulationSpeciesId = speciesId,
                    });
                });

            ExpectRejectedByBothPersistencePaths(
                directory,
                "population-overflow",
                (state, speciesId, colonyDesign) =>
                {
                    state.ActiveDesignId = "warp_scout";
                    state.ActiveBuildProgress = 0.0;
                    state.ReservedPopulationMillions = 0.0;
                    state.ReservedPopulationSpeciesId = null;

                    for (var i = 0; i < ShipyardState.MaxPendingBuilds - 1; i++)
                    {
                        state.QueuedBuilds.Add(new ShipBuildOrderState
                        {
                            DesignId = "warp_scout",
                            ReservedPopulationMillions = 0.0,
                            ReservedPopulationSpeciesId = null,
                        });
                    }

                    state.QueuedBuilds.Add(new ShipBuildOrderState
                    {
                        DesignId = colonyDesign.Id,
                        ReservedPopulationMillions = colonyDesign.PopulationCostMillions,
                        ReservedPopulationSpeciesId = speciesId,
                    });
                });
        });
    }

    private static void ValidateZeroPopulationOverflowCanStillBeBoundedSafely()
    {
        WithTemporaryDirectory(directory =>
        {
            var galaxy = CreateGalaxy();
            var state = galaxy.ShipyardStates[0];
            state.ActiveDesignId = "warp_scout";
            state.ActiveBuildProgress = 0.0;
            state.ReservedPopulationMillions = 0.0;
            state.ReservedPopulationSpeciesId = null;

            // One more queued item than the logical 7-slot queue is harmless here because every
            // overflow entry carries zero population. Persistence may clamp metadata, but not people.
            for (var i = 0; i < ShipyardState.MaxPendingBuilds; i++)
            {
                state.QueuedBuilds.Add(new ShipBuildOrderState
                {
                    DesignId = "warp_scout",
                    ReservedPopulationMillions = 0.0,
                    ReservedPopulationSpeciesId = null,
                });
            }

            var v8Path = Path.Combine(directory, "zero-pop-overflow-v8.json");
            var v8 = new CampaignSaveService();
            v8.Save(v8Path, galaxy, 64.0);
            var loaded = v8.Load(v8Path);
            var loadedState = loaded.Galaxy.ShipyardStates.First(s => s.CivilizationId == state.CivilizationId);
            Require(
                loadedState.PendingBuildCount == ShipyardState.MaxPendingBuilds,
                "zero-population overflow did not round-trip to the bounded pending-build maximum");
            Require(
                loadedState.QueuedBuilds.All(build => build.ReservedPopulationMillions == 0.0),
                "zero-population overflow unexpectedly retained or created physical population");

            var v9Path = Path.Combine(directory, "zero-pop-overflow-v9.json");
            new CampaignStatePersistenceService().Save(
                v9Path,
                galaxy,
                simulationDays: 64.0,
                new DiplomacyState());
            var loadedV9 = new CampaignStatePersistenceService().Load(v9Path);
            var loadedV9State = loadedV9.Galaxy.ShipyardStates.First(s => s.CivilizationId == state.CivilizationId);
            Require(
                loadedV9State.PendingBuildCount == ShipyardState.MaxPendingBuilds,
                "v9 wrapper did not preserve the bounded zero-population queue semantics");
        });
    }

    private static void ExpectRejectedByBothPersistencePaths(
        string directory,
        string scenario,
        Action<ShipyardState, string, ShipDesignDefinition> mutate)
    {
        ExpectRejected(
            directory,
            scenario + "-v8",
            mutate,
            useCampaignV9: false);
        ExpectRejected(
            directory,
            scenario + "-v9",
            mutate,
            useCampaignV9: true);
    }

    private static void ExpectRejected(
        string directory,
        string scenario,
        Action<ShipyardState, string, ShipDesignDefinition> mutate,
        bool useCampaignV9)
    {
        var galaxy = CreateGalaxy();
        var shipyard = galaxy.ShipyardStates[0];
        var civilization = galaxy.Civilizations.First(c => c.Id == shipyard.CivilizationId);
        var colonyDesign = ShipDesignRegistry.All.First(design => design.PopulationCostMillions > 0.0);
        mutate(shipyard, civilization.SpeciesId, colonyDesign);

        var path = Path.Combine(directory, scenario + ".json");
        InvalidOperationException? rejection = null;
        try
        {
            if (useCampaignV9)
            {
                new CampaignStatePersistenceService().Save(
                    path,
                    galaxy,
                    simulationDays: 42.0,
                    new DiplomacyState());
            }
            else
            {
                new CampaignSaveService().Save(path, galaxy, simulationDays: 42.0);
            }
        }
        catch (InvalidOperationException ex)
        {
            rejection = ex;
        }

        if (rejection is null)
        {
            throw new InvalidOperationException(
                $"{scenario} persistence silently accepted population-losing shipyard state");
        }

        Require(
            rejection.Message.Contains("reserved population", StringComparison.OrdinalIgnoreCase) ||
            rejection.Message.Contains("reserved colonists", StringComparison.OrdinalIgnoreCase) ||
            rejection.Message.Contains("overflow", StringComparison.OrdinalIgnoreCase),
            $"{scenario} was rejected for an unrelated reason: {rejection.Message}");
    }

    private static Game.Simulation.Models.GalaxyState CreateGalaxy() =>
        new GalaxyGenerator().Generate(
            0x5350_5253_4953_544CL,
            new GalaxyGenerationSettings
            {
                SystemCount = 36,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 420.0f,
            });

    private static void WithTemporaryDirectory(Action<string> action)
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            "stellar-continuum-shipyard-save-validation",
            Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            action(directory);
        }
        finally
        {
            if (Directory.Exists(directory))
                Directory.Delete(directory, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
