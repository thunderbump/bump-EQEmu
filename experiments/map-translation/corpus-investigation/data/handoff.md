# Investigation handoff

2026-11-10 central-42r2. Root central-fnma and child completion remain with parent.

- Worktree: /home/bump/.t3/worktrees/bump-EQEmu/t3-024c886a.
- Branch: t3/map-corpus-data-gaps. Investigation source/report commit
  44da8b145dcc8e9709f2c5fa647fc6d4a5fd760b, pushed.
- Native source: 2d6cc88a5ea891f79b72fc0e9530b35d7b2caec8, unchanged production
  decoder. Executable SHA256 e7fae440d22a42b2a92049a1fd0ff86d4e2978d513020a2c33356b22baa8b3ef.
  No temporary instrumented source or candidate raycaster execution.
- Final worker command: `python3 experiments/map-translation/corpus-investigation/data/transport.py reviewed`.
  Fresh reproduction requires a different absent identifier and dedicated daemon/import.
  Full output: ~/.local/state/eqemu-map-translation/corpus-data-gaps-20261110/reviewed.
  Four official exports matched retained meshes exactly. Independent fixed-offset
  assertions passed. Exit 0/no OOM; memory peak 309940224 bytes.
- Exact provenance and identities are in findings.json, inputs.json,
  independent-excerpts.json and execution.json. README records cause, confidence,
  minimum followup, empty selection uncertainty and unsafe native empty boundary.
- All worker containers removed and image store pruned. Dedicated service stopped
  and verified inactive. Cleanup receipt is included; previous evidence untouched.
- Operations authoritative worklog:
  content/projects/bump-akk-stack/worklog/2026-11-10-map-corpus-gap-investigation.md,
  published/pushed at c3761f7c255ff7b4bd08a4c0a6efa4ac6589b9c4 in operations-webui.
  Includes parallel limit report d28e9ce0f78023e493bed6a3ed81125391f9ff83,
  distinguishing setup failure from candidate failure. Authority validation,
  81 tests, build, refresh, compose redeploy and HTTP 200 all passed after update.
- No new PR. PR108 linked and unwatched. No merge, game deployment or limits
  implementation. Unrelated Server and Operations edits were not staged or changed.
