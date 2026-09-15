# Exploration advancement validation

The integrated `ExplorationSimulation::advance` port matches 89 actual-C# cases, including eight source failures and every one of the twelve exploration event types. Three additional C# null-galaxy observations are retained as source-only boundaries; two separate native safety checks validate nonfinite computed lane geometry and mission-revision exhaustion. Fixture SHA-256: `F4206ABCD0AE154B76276DF83B32F220622B966D3AB7034612AC565719D2EAB8`.

The retained C# generator reproduces that hash. Standalone Release and Debug pass with `/W4 /WX`; Debug also uses `/RTC1`. Maintained integration passes 34/34 CTest and 19/19 Python checks in `work/native-025-live-testing.log` before subsequent gates are added.

Coverage includes resumed local departure, warp and local arrival; travel through intermediate systems; fuel consumption and actual inbound refueling; hold and queued return; funded operating time; scout reconnaissance and science surveys; one-way contacts; AI target selection; body-specific resource, anomaly and native-population discoveries. Full mutable fleet state, including combat/loadout/vessel history, and observer knowledge are compared. Read-only view inputs are decoded and source-frozen, not described as a second native serializer for every immutable field.

When the source throws, earlier state mutations remain but the method-local event list is not returned. The native port and test harness preserve that distinction, including a revision-exhaustion failure after physical arrival and knowledge changes. Native exception categories are checked explicitly; an unexpected exception cannot be treated as an expected InvalidOperation failure.

This is the exploration subsystem's actual advancement, not the complete campaign scheduler or player save migration. Freight, research, construction, combat, colonization and strategic AI must still be composed in the source coordinator's order before claiming a full native tick.

## Clean 0.1.14 evidence

Clean benchmark `Builds/Windows/StellarContinuum-windows-benchmark-e4e37db8-20260913T052237075636Z` records source `e4e37db8072e1ef8f3b139582dbade640338e463`, `sourceDirty: false`, seven files, 35/35 CTest, 19/19 Python, and relocated validation true. Log: `work/native-014-clean.log`.
