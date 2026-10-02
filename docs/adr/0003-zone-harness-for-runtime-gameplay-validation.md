# Zone Harness for Runtime Gameplay Validation

Runtime gameplay validation will use a repo-owned **Zone Harness** as test infrastructure rather than relying on manual client testing for every server-observable scenario. Harness scenarios should run in a separate one-off zone process by default, use HTTP only as a thin transport over typed in-process scenario, perception, action, and event interfaces, prefer synthetic in-process owners for server-observable behavior, and reserve persistent dev runtime or real-client checks for world, login, zoning, packet, UI, and final visual validation.

**Consequences**

- Bot and **Autonomous Actor** adjacent behavior can gain faster feedback through repeatable scenario tests.
- Harness setup and reset shortcuts must stay visibly separate from **Actor Actions**, which should remain intent requests through ordinary server behavior.
- Live client smoke tests still matter for behavior that depends on packet handling, UI rendering, login, zoning, or player-visible quirks.
- The first full harness scenario should prove bot slow maintenance against **Engaged Hostiles** before broader actor scenarios are added.
