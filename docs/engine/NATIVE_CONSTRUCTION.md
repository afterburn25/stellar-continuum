# Native construction workspace

The C++ client provides a full construction workspace through the existing
campaign simulation. It lists only currently available projects and projects
already active, queued or completed for the player. Future locked project names
are not disclosed.

Each project shows its source description, requirements, sovereign-currency
authorization, upkeep, industry requirement, available industry and minimum
remaining time. Active and queued projects show their reserved authorization,
current refund and progress. The minimum time assumes sufficient industry;
material shortages can extend it. Research facilities, colony production and
research capability rules remain authoritative.

Start and Queue use the same Core preparation as the actual command. A preview
copies construction state before virtually promoting a queued head; it does not
mutate the campaign. Live commands preserve the original promotion before later
denials or exceptions. The existing queue limit, costs, prerequisites and
industry consumption are unchanged.

The controller owns its projected values and rejects commands from an old
campaign or altered quote. Ordinary affordable balance changes are rechecked
against the current economy. Successful commands consume the previous quote.
No simulation references survive a projection call.

Cancel first pauses the normal strategic clock and refreshes the exact refund.
The second click confirms cancellation. A changed construction revision disarms
confirmation. The player explicitly resumes the campaign afterward; there is
no separate construction pause/resume command in Core.

The status panel presents active work first, then queued work in canonical queue
order, then completed projects. Both hit testing and scrolling use the rendered
row spacing. Progress bars remain clipped and handle nonfinite fractions.
Research, shipyard, construction and map input contexts are mutually exclusive.

## Validation

- The construction Core extraction and native controller passed strict Debug
  and Release checks, including promotion before a later rejection or exception,
  funding changes, refunds, currency/generation changes and owner-thread checks.
- The unchanged actual-source construction oracle passed all 46 cases in both
  configurations.
- Workspace checks remain active in Release. An intentionally inverted Release
  check failed with its expression and line, as required.
- A 720p regression verifies a 14-row list can reach its final order and that
  active/queued/completed ordering agrees with mouse selection.
- Actual staged Vulkan checks started an orbital shipyard while the campaign
  was running, prepared a paused cancellation quote, captured the screen and
  saved the order. The orbital capability and completed launch complex in this
  scenario are explicitly test-authored. They are not fresh progression proof.
- Combined Engine 0.1.48 validation passed 125/125 CTest and 83 Python
  checks. Maintained export now exercises five actual production UI launches
  including paid orders and full paused reload/resave. Exact export follows.

Reviewed source manifests are in the ignored working evidence directories
`work/113-native-construction` and `work/114-native-construction-ui`. The corrected
UI manifest is `fe1acbfb7af6f4e8b21360c81a87f8a015b9c68c24357385a9b74142ab29282d`.
The maintained tests and build declarations carry the reusable checks.

## Remaining work

Prove ordinary fresh research-to-construction-to-ship progression and surface any
canonical research dead ends. Native system/planet/colony placement views and
production graphics are separate unfinished work. This workspace does not add
physical station placement or replace the existing game's construction rules.
