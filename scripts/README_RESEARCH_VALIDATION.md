# Adaptive Research Validation Stack

Research design/data changes on `dev/adaptive-research` must pass the full validator/benchmark stack before merge:

1. `validate_research_catalog.py` — possibility graph, IDs, prerequisites, pressures, evidence/applicability references, cycles, counts.
2. `validate_research_maturation.py` — cross-lineage capabilities, maturation states/outcomes, structural grants.
3. `validate_research_competence.py` — knowledge fields, competence, research facilities, tacit knowledge, Project Readiness.
4. `validate_research_transfer_ui.py` — foreign technology, transfer/licensing packages, research UI secrecy/layout rules.
5. `validate_research_start_runtime.py` — starting-history compositions, prerequisite closure, runtime integration boundary, materialized research view contract.
6. `validate_research_agenda_ai.py` — research agenda priorities, mutable scientific culture, natural complacency/catch-up guardrails, fair-information AI planning, runtime/view integration.
7. `validate_research_benchmarks.py` — deterministic offline 500-year divergence, 350-year complacency/response, foreign-tech asymmetric-value, and 1,000-year bounded-state soak scenarios.

CI runs these before .NET restore/build and the pinned Godot editor/runtime smoke tests.

The benchmark harness is an offline design/engineering tool and may scan the public catalog directly. It is not the gameplay runtime, which remains event/index-driven.

A validator/benchmark should be strengthened when a new research schema gains stable references. Do not weaken a failing gate merely to make a PR pass; correct the underlying data or explicitly justify an architectural/balance threshold change.
