# Colony surface shortage feedback

Core combined validation: simulation 70/70, Core runtime 74/74, quality 19/19.
Reserve preview and advancement share one pure calculation; only advancement
assigns the exact resulting inventories. Regression coverage checks depletion,
refill, partial-day exhaustion and mutation-free observation.

The colony surface now projects its status from `ColonySurfaceFeedbackReadModel`. It owns no
state and does not call a mutating simulation step.

- Sustenance uses a read-only one-day preview of the authoritative reserve interval. `Preview`
  and `Advance` share one pure reserve calculation; only `Advance` writes its exact resulting
  food and water quantities. A reserve
  that runs out during that day can show decline; a fully covering reserve is **buffered**.
  Recovery follows the actually unsupported resource before the long-term capacity limiter.
  Outposts only show their support limit.
- Each incomplete site shows stored materials, total surface-site demand, and a minimum full-
  supply build time. It deliberately does not estimate a current allocation rate because the
  coordinator also applies production, empire projects, shipbuilding, and policy competition.
  Zero stock with recent production is described as awaiting produced material, never as a deadlock.
- Build cards still authorize from credits alone. Their tooltips make clear that materials are
  spent gradually and shared with existing authorized sites.

The sidebar keeps recovery actions concise: controlled agriculture for food, water reclamation for
water, habitat complexes for housing, and material production or fewer competing sites for
construction. This is presentation-only feedback; construction authorization, allocation,
population, save data, and balance remain authoritative elsewhere.
