# Construction queue and recovery

Core integration branch: `work/core-construction-colony-recovery`. Combined
simulation 70/70, Core runtime 74/74 and quality 19/19 pass. The maintained
`ScreenshotCapture.ConstructionRecovery.cs` drives real mouse input at 720p to
authorize and cancel orders, compare exact payments/refunds/material stocks and
verify the ordinary Save output. `STELLAR_CAPTURE_FOCUS=construction` runs this
journey independently; the full capture runs it without adding required images.
Hosted and final native results belong in the integration PR before merge.

Construction orders retain the exact authorization quote paid at placement. The active order also stores that quote; legacy active orders restore with a zero quote so cancelling them cannot create credits.

`QueueProject` keeps at most eight unstarted orders, debits each quote once, and promotes the FIFO head only after it remains eligible. A blocked head stays visible in state for the presentation layer and can be cancelled for its full recorded quote. Active cancellation refunds the paid quote in proportion to unconsumed material work; consumed materials are never refunded.

AI automatic construction now goes through `StartProject`, selecting only affordable eligible catalog projects. It no longer assigns an active project directly.

The existing v12 envelope remains compatible because the new DTO fields are optional/defaulted. This is an additive field change inside an already-supported envelope, so it does not change the format version. Loading validates finite values, known project IDs, duplicate/overlapping projects, and queue bounds. Stored paid quotes are deliberately not compared to current catalog prices: a later balance revision must not alter a saved authorization or make an older campaign unloadable.
