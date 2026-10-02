# Async fallback dialogue generation

Generated **Fallback Dialogue** for NPCs and bots will be requested asynchronously and emitted later as **Delayed Dialogue** instead of blocking zone chat handling on the remote Ollama call. Synchronous generation would be simpler, but remote responses can take long enough to stall gameplay; the async approach keeps normal chat handling responsive and accepts the added need for **Current Interaction** checks before speaking or emotes are emitted.

**Consequences**

- The first implementation must validate that the original speaker and target still form the **Current Interaction** before emitting a delayed response.
- Remote failures should produce an **Unavailable Reply** only when the target still exists and the interaction remains current.
- Operators configure the feature through **Dialogue Rules** rather than hardcoded endpoint settings.
