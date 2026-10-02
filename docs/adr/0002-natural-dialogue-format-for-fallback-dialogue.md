# Natural Dialogue Format for Fallback Dialogue

**Fallback Dialogue** will use **Natural Dialogue Format** for generated **Dialogue Responses** rather than requiring structured JSON output. The first remote response service target is a smaller local model optimized for response speed, and strict structured output is likely to be less reliable with that class of model; server-side **Dialogue Response Processing** will parse simple **Stage Direction Markers** and reject unsafe output instead.

**Consequences**

- **Dialogue Responses** can remain natural in-character text while still producing ordered **Dialogue Fragments**.
- Structured formats such as JSON can be revisited later as a different remote response adapter or prompt strategy.
- Safety stays in server-side **Dialogue Response Processing**, not in trusting the model to follow a rigid schema.
