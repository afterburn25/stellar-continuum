# Freight migration contract (draft gate 026)

This gate ports `FreightSimulation` only: manual logistics transit and collection
orders, daily cargo transfer, and its three transfer-rate helpers. It borrows the
existing galaxy collections and lane graph. The default reach path is the native
operational-reach adapter; tests may inject an assessor with the same inputs and
typed result.

Collection eligibility follows source `FirstOrDefault` ordering. A bulk freighter
must be active, owned, Logistics-role, idle by all three assignment fields and by
strictly-positive cargo, physically between no lane legs, and located at a developed
colony. The chosen resource outpost must be owned and represented, and its actual
operations snapshot must have stored material or positive extraction. Once reach
is supported, freight home and target IDs are written before route assignment.

Advance validates finite nonnegative time before its zero-time return, then visits
fleets in storage order. Funding is a binary gate at `1e-7`; it never scales transfer
rate. Loading and unloading are genuinely time- and endpoint-capacity-limited.
Loading mutates outpost storage and fleet cargo before return reach is assessed.
On a supported return, the target outpost ID is cleared before route assignment.
These partial mutations survive a later dependency failure.

Source `Math.Min` and `Math.Max` propagate NaN. The native port does likewise and
does not substitute `std::min`/`std::max` behavior. Duplicate IDs preserve the
source's first-match behavior. Missing economy during unloading is an operation
error after earlier fleets may already have mutated.

Native route assignment rejects `INT_MAX` mission revisions before route mutations
to avoid signed overflow. C# unchecked arithmetic wraps. Boundary tests therefore
record the native error separately while requiring the source-ordered freight ID,
cargo, storage, and target-clear mutations that precede assignment.

This gate does not advance lane travel, extract resources, finance operations, or
apply industry storage caps. Null `GalaxyState` is a source-only reference boundary;
the native typed view cannot represent it.
