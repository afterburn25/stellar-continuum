# Native colony and settlement controllers

The C++ client projects owned colony operations and prepares ship-delivered
settlement orders through existing Core plans. The controllers return owned
values, stay on the simulation owner thread, and bind campaign generations.
They retain no world pointers across commands or reloads.

## Owned colony telemetry

`NativeColonyController` admits a body only from the current observer-safe
system snapshot and current known system, with a matching player-owned colony.
Population, labor, sustenance capacities, surface power, production, upkeep,
specialization, extraction, hub capacity and concurrent construction sites
come from canonical models. Site changes, including location and rotation,
invalidate the view revision. Site snapshots remain independent of later world
mutation. Food and water reserve days describe current stored population-days
divided by current population, matching the preserved presentation; they are
not tomorrow's simulated reserve balance. Surface production is not total
civilization income or treasury surplus.

## Ship-delivered settlement

`NativeSettlementMissionController` copies the canonical colony/outpost
opportunity plans for active owned populated settlement vessels. Suggested
opportunities retain the planner's bounded list, while manual targeting uses
an exact body assessment without that cap. The existing coordinator owns
knowledge gates, species viability, deposits, reach, costs and mission admission.

An order binds generation, player, view revision, fleet/order revision, design,
personnel, currency and cost, then re-resolves the vessel and current plan before
calling the coordinator. Funding loss or revoked knowledge cannot reuse an
old preview. Ordinary treasury changes do not invent new eligibility rules.
Core retains travel, 30-day colony/20-day outpost establishment and vessel
consumption. No settlement is created by the client directly. Shared Core
authorization terms quote the full initial charge and zero additional charge
for an already authorized retarget. Live selected-vessel status requires no
opportunity-planner search.

The focused controller checks include fresh campaign telemetry, changed site
revisions, current reserve values during a deficit, independent snapshots,
owner/thread/generation gates, finite paid colony and outpost missions,
CampaignFrame advancement, establishment, vessel consumption, and stale
funding/knowledge/order rejection. Authored fast finite vessels bound test
runtime; they are not evidence of ordinary starting-campaign unlock progression.
The maintained colonization oracle retains 124 actual-source cases plus four
native boundaries. Exact combined results belong in the checkpoint handoff.

Surface mutation is handled separately by NATIVE_SURFACE_ASSESSMENTS.md.
See NATIVE_SETTLEMENT_WORKSPACE.md for the manual ship-to-planet workflow.
