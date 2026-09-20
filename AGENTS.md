# Required engine capability workflow

The user's Engine Capability Gap Rule applies to Stellar Continuum work in this
repository. See [the capability registry](docs/ENGINE_CAPABILITIES.md) and
[engine architecture](docs/engine/ARCHITECTURE.md) before extending a subsystem.

- Requested functionality must use real authoritative game state and commands.
  Do not substitute static mockups, permanently disabled controls, hidden
  omissions, duplicate simulation, or private UI versions of shared rules.
- When required support is missing, first inspect ownership, existing APIs,
  dependencies, saves, threading, determinism and performance. Extend the
  authoritative subsystem incrementally; do not replace it speculatively.
- Make the extension reusable, deterministic where it affects simulation,
  compatible with save/load and existing consumers, performant and testable.
  Add meaningful automated coverage, integrate consumers, then regress them.
- Update `docs/ENGINE_CAPABILITIES.md` with purpose, modules, public interfaces,
  consumers, tests, save/performance impact, limitations and future reuse.
- Missing required engine support means the affected feature remains unfinished.
  Do not call a UI-only implementation complete or silently reduce the request.
  Default to implementing the support; if it cannot safely be completed, report
  the concrete architectural blocker and reason explicitly.
- Reports of engine work must include **ENGINE CAPABILITIES ADDED / EXTENDED**
  and **ENGINE LIMITATIONS REMAINING**, covering the registry details above.
  Passing tests is not evidence that unimplemented requirements are complete.

Preserve intentional work already in progress. Do not revert other changes or
restart the native conversion to apply these rules.
