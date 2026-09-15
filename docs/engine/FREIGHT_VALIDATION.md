# Freight validation

Freight parity covers 87 actual C# cases plus two separately counted native revision-exhaustion boundaries. Fixture SHA-256: `A701B151AE622358936F1BE6CD68CEAD956F96166A7CAF50FD66FC5DAB19DD5F`. The retained generator reproduces the hash, standalone Release/Debug pass with `/W4 /WX`, and the combined maintained run passed 37/37 CTest and 19/19 Python checks in `work/native-026-028-live-testing.log`.

Coverage includes real-body positive extraction, zero stored powered behavior, parameter and ID failure messages, NaN computed reserve, typed callbacks outside operation catches, and exact partial state after failures.
