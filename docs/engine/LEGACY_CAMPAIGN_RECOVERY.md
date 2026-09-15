# Gate 085: legacy campaign recovery helpers

This isolated gate reconstructs four private `CampaignSaveService` recovery helpers. It creates initial observer knowledge, migrated legacy technology and construction state, and legacy expansion colony fleets. It does not choose save-version policy or decode a campaign payload.

The three creation functions return owned values. Expansion recovery borrows systems and civilizations while mutating caller-owned fleets and colonies. Source order is observable: population is deducted after species resolution and before the home-system `First` lookup, so a missing home leaves that deduction in place.

The expansion adapter preserves stable descending `double` ordering, including finite values before NaN and input order for ties. Fleet IDs use explicit 32-bit wrapping for the source's unchecked addition. Newly created fleets retain the source `FleetState` defaults and null `DesignId`; only the fields assigned by the helper and its combat profile are populated.
