# Shipyard order recovery

New shipyard orders persist a stable order ID, paid authorization quote, and exact population-source colony. Cancellation uses that identity so a stale control cannot cancel a promoted duplicate design. Queued orders refund their recorded quote; active orders refund only the unbuilt material fraction. Reserved colonists return only to the same owned colony with the same species. If that source cannot be proven, cancellation is rejected without changing credits, population, or the order.

Older saves default quote and source fields to zero/null. They remain playable and can complete, but a legacy population reservation without a provable source cannot be cancelled.

The simulation exposes one pure cancellation assessment for both the dashboard preview and the cancellation command. It preflights the exact order, refund arithmetic, treasury capacity, population destination, and queued-order promotion before any mutation. Order allocation likewise validates the deterministic sequence and all existing identities before debiting credits or population.

Save and load boundaries reject non-finite or negative accounting, malformed or duplicate effective identities, counters that trail canonical IDs, impossible progress, and invalid source ID values. Missing historical source colonies are deliberately loadable because ownership can change during play; cancellation stays blocked while ordinary completion remains available. Legacy zero-metadata invalid designs retain the prior sanitize-on-load behavior, while records carrying refund or population metadata fail closed rather than losing value silently.

Persisted order identities use only ASCII letters, digits, hyphens, and underscores so they are safe in the existing Godot cancellation node names. Queue overflow may be sanitized only when it has no quote, identity, or population payload. The maximum sequence value is a persisted exhausted marker: the preceding identity can round-trip, while later orders reject cleanly before any debit.

Historical negative population placeholders normalize to zero only when the record has no paid quote, order identity, source colony, or species metadata. Any such recoverable metadata keeps the strict nonnegative accounting requirement.
