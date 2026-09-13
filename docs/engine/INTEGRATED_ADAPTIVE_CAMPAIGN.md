# Gate 075 integrated Adaptive campaign

`IntegratedAdaptiveCampaignRuntime` is the stable owning composition recovered
from `Main.CoreIntegration.cs`. Its final heap allocation owns the Adaptive
Research strategic runtime, fresh-campaign simulation state, schema-2 Adaptive
Research campaign state, separately supplied Diplomacy state, capability
views, Diplomacy runtime, and galaxy coordinator. Moving the outer owner keeps
borrowed addresses stable. Moving or replacing an individual borrowed member
invalidates its dependents.

Fresh and schema-2 research-restore factories are separate. The restore factory
still receives its Diplomacy state independently. Neither factory is a
player-save17 compatibility surface.

The core coordinator disables legacy research and the coupled legacy science
accrual. Ordinary construction, ordinary shipbuilding, and strategic AI
shipbuilding use the same Adaptive Research campaign. Strategic AI reads only
observer-filtered Diplomacy knowledge. Combat previews and command issuance use
the callback created by the same Diplomacy runtime.

Advance runs core first. For positive elapsed time it records sensor contacts
in stable numeric civilization order at the supplied absolute end day, using
`tech:quantum_sensors` or `tech:distributed_sensor_network`. It then advances
Adaptive Research and finally processes core exploration/combat events through
Diplomacy at the same end day. No fabricated clock is retained. Callers may
pass an owned `IntegratedAdaptiveCampaignAdvanceTrace` to `advance`; it is
cleared at call start and receives only completed phase outputs, so partial
mutations remain inspectable when a later phase throws. The default null trace
keeps normal gameplay free of diagnostic event-vector copies.

See `VALIDATION.md` for the exact managed fixture, row coverage, deterministic
hash, and strict native results.
