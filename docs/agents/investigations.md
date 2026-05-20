# Investigation Notes

Reusable findings from agent investigations that are useful outside a single task.

## Codex Flex Service Tier

Codex CLI uses the `service_tier` config key for service tier selection.

Use Flex for one invocation:

```sh
codex -c 'service_tier="flex"'
codex exec -c 'service_tier="flex"' "your prompt"
```

Persist Flex as the default in `~/.codex/config.toml`:

```toml
service_tier = "flex"
```

Or define a selectable profile:

```toml
[profiles.flex]
service_tier = "flex"
model = "gpt-5.5"
model_reasoning_effort = "medium"
```

Then run:

```sh
codex --profile flex
```

Codex has an interactive Fast mode command: `/fast on`, `/fast off`, and `/fast status`.
No equivalent `/flex` toggle was found in the Codex docs or `codex-cli 0.128.0` help output.
Use `-c service_tier="flex"`, a profile, or the config file for Flex.

Sources checked:

- OpenAI Codex config reference: `https://developers.openai.com/codex/config-reference`
- OpenAI Codex speed docs: `https://developers.openai.com/codex/speed`
- OpenAI Flex processing docs: `https://platform.openai.com/docs/guides/flex-processing`
