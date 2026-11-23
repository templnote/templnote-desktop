# Headless engine architecture

This document describes the headless editor engine and the AI/multi-user layers
built on top of it (P0–P5), plus the in-process desktop agent and its security
model. The QML editor remains the renderer; an `EditorSession` is the
authoritative document service in both the desktop app and the daemon.

Build requirement: Qt 6 with the **HttpServer** module installed (CMake component
`HttpServer`, linked as `Qt6::HttpServer`).

## Layers

```
templnote_domain   QtCore-only: Block, Document, AppPaths
templnote_engine   QtCore-only: Commands, Ops, Patches, EditorSession,
                                JsonDocumentState, CrdtDocumentState,
                                ChangeFeed, CapabilityGate
templnote_store    QtCore+Sql:  DocumentStore (CAS writes, journal, snapshots)
templnote_sync     QtCore+Net:  SyncManager (LAN CRDT op exchange)
templnote_ai       QtCore+Net:  ToolSchema, ModelRouter, PipelineRunner,
                                AgentHttpServer, AgentService
templnote_core     existing GUI/QML code (BlockModel now also accepts patches)
```

## P0 — Engine

- `src/engine/Types.h`: `Actor`, `Selection`, `Op`, `Command`, `Patch`,
  `Result<T>`, `SnapshotInfo`. Every edit is a typed `Command`; every change is
  an actor-tagged `Op` inside a `Patch`.
- `src/engine/Commands.cpp`: pure command application (insert/delete/replace,
  markdown format toggles, block operations, metadata). JSON round-trips are
  the future AI tool wire format.
- `src/engine/EditorSession.cpp`: transaction boundary. Stamps causality,
  emits `patchApplied`, tracks selection, bounded undo/redo, pluggable state
  backend.
- `JsonDocumentState` wraps the original `Document` for single-user parity.

## P1 — Store

`src/store/DocumentStore.cpp`:
- Revisioned `.templnote` files (`tnRevision`, `tnLamport` fields, backward
  compatible).
- `QLockFile` + compare-and-swap: stale writers get a `conflict` error.
- `QSaveFile` atomic writes.
- SQLite journal of every `Patch` (provenance) and a snapshot per revision.
- `restore(snapshotId)` writes an older snapshot as a new revision.

CLI: `templnote-headless apply|script|versions|serve`.

## P2 — Model cutover

`BlockModel::applyRemoteDocument(documentJson, patchJson)` applies engine
snapshots. When block ids are unchanged it emits granular `dataChanged`
signals (preserving delegates and caret); structural changes use a model
reset. `m_applyingRemote` suppresses local undo entries and the dirty flag.

`src/bridge/LiveBridge.cpp` owns an `EditorSession` + `ChangeFeed`; each patch
is pushed into the open `BlockModel`. Instantiated as `agentBridge` in
`Main.qml`.

## P3 — Change feed

`ChangeFeed` records the last 500 patches, emits `agentChange` for
agent/pipeline actors, and supports lookup by actor. This is the provenance
stream for "what did the AI change".

## P4 — Multi-user

- `CrdtDocumentState`: op-set CRDT. Every replica keeps all causally-tagged
  ops; the document is rebuilt deterministically. Block order uses RGA-style
  insertion after stable ids; text/fields use per-field LWW registers keyed by
  `(lamport, actor)`. Idempotent merge: replicas that saw the same ops converge.
  Incremental application keeps the common path fast (see benchmark).
- `SyncManager`: length-framed TCP exchange of op sets. Binds to `127.0.0.1:45210`
  by default and authenticates peers with a token handshake; use
  `--allow-lan` plus WireGuard/Tailscale for remote access (no cloud server).
- `CapabilityGate`: Owner/Editor/Viewer roles. AI-originated commands require
  the `ControlAi` capability (owner only). It is installed on `EditorSession`
  itself, so local commands, remote patches, phone commands and pipeline stages
  all pass through the same check. A caller cannot claim an identity by putting
  an `actor` field in a request: the session stamps its own trusted actor first.

## P5 — AI

- `ToolSchema`: OpenAI-compatible tool definitions for every edit command;
  parses model tool calls back into validated `Command`s.
- `ModelRouter`: detects local Ollama (`/api/tags`), falls back to OpenRouter
  only when explicitly enabled. API key stays on the host.
- `PipelineRunner`: sequential stages (write → grammar → humanizer → grammar →
  validation), each acting under a distinct pipeline actor id so provenance
  records which stage changed what. Each stage is a real tool-use loop: tool
  results are fed back to the model until it stops calling tools, with
  configurable tool-round and revision budgets, cancellation, token accounting
  and SSE stage events.
- `AgentService`: in-process desktop agent. It owns the router/pipeline, seeds a
  preview `EditorSession` from the open editor's live document, and mirrors the
  result through `BlockModel::applyAgentDocument()`. The prompt-panel plugin
  calls `agentService.sendPrompt()`; no separate daemon is needed for desktop
  use.
- `AgentHttpServer`: authenticated HTTP control API for phone clients:
  `GET /document`, `/changes`, `/status`, `/tools`, `/providers`, `/models`,
  `/events` (SSE), `POST /command`, `/prompt`, `/cancel`, `/plugin-tool`,
  `/provider/*`, `/model/*`.

## Running the agent daemon

```sh
# Loopback only by default. A bearer token is generated and printed on stderr
# when --token / TEMPLNOTE_AGENT_TOKEN is not supplied.
templnote-headless serve --port 8787 --sync-port 45210 --file /path/doc.templnote

# Explicit LAN exposure (use a VPN tunnel and a strong token):
templnote-headless serve --allow-lan --token <token> --file /path/doc.templnote
```

Phone on the same LAN (or over a WireGuard tunnel):

```sh
curl -H 'Authorization: Bearer <token>' http://<host>:8787/document
curl -X POST -H 'Authorization: Bearer <token>' -H 'Content-Type: application/json' \
  -d '{"type":"setText","blockId":"b1","text":"From phone"}' \
  http://<host>:8787/command
curl -X POST -H 'Authorization: Bearer <token>' \
  -d '{"prompt":"Rewrite the intro"}' http://<host>:8787/prompt
```

Every committed patch is written back with compare-and-swap through
`DocumentStore`, and the daemon reloads the file before a mutating request if
the desktop app saved in the meantime. AI edits are therefore durable and do not
silently overwrite GUI edits.

## Security model

- **Transport:** both the HTTP control API and the CRDT sync server bind to
  loopback by default. LAN exposure is an explicit `--allow-lan` opt-in. All HTTP
  requests require a constant-time-checked bearer token; the sync protocol has a
  token handshake. CORS is never `*`: origins are reflected only when one is
  explicitly configured.
- **Actor identity:** `/command` discards caller-supplied `actor` and `lamport`
  fields; the trusted session actor is stamped by `EditorSession`. All mutations
  then pass through `CapabilityGate` at the session choke point.
- **Keys:** API keys stay in `SecretStore`. `connectProvider` (GUI or HTTP)
  accepts only HTTPS or loopback endpoints, so a key is never sent in cleartext
  to an arbitrary host.
- **Plugins:** `approvePlugin` is not an agent tool and is not reachable over
  the control API. The user approves AI-authored plugins in Settings, where the
  agent-written source is shown for review. Agent tools may only modify
  unapproved, disabled, agent-created plugins. QML `XMLHttpRequest` is blocked
  from non-loopback hosts and `file:` URLs at the engine level.
- **Robustness:** the control API uses Qt's `QHttpServer`, which handles
  chunked transfer encoding, keep-alive and standards-compliant parsing.
  `QHttpServerConfiguration` caps body size (8 MiB), headers, URL length,
  connections per host and request rate. Provider connection checks no longer
  block the event loop, and long-running stage events stream over SSE.
  Sync buffers are per-peer with a frame-size limit.

## Reverse wiring (complete headless desktop)

`BlockModel` no longer owns editing state. It holds an `ExternalDocumentState`
pointing at its own `Document`, and an `EditorSession` that operates on that
same instance:

- Every edit method (`updateBlockText`, `changeBlockType`, `setBlockChecked`,
  `splitBlock`, `moveBlock`, column operations, …) now applies an `Engine::Command`
  or records a full-document `ApplyJson` snapshot through the session.
- Human edits therefore appear in the patch stream, the journal, the CRDT op
  set and the agent's document context — exactly like AI/phone edits.
- The session uses lightweight history (`setLightweightHistory(true)`) so the
  per-keystroke path stays O(changed block) rather than O(document).
- `BlockModel::engineSession()` exposes the authoritative session to agents and
  tests. `tests/tst_blockmodel.cpp` verifies the engine sees human edits and
  that the two layers share one `Document`.

## Plugin system (agent-authorable)

- `PluginManager`: `createPlugin(spec)`, `writePluginFile(id,path,contents)`,
  `rescanPlugins()`, `validatePlugin(id)`, `approvePlugin(id)`,
  `rollbackPlugin(id)`, `declarativeSettings(id)`, `pluginPermissions(id)`.
- `manifest.json` supports declarative `settings.fields` and `permissions`.
  `DeclarativePluginSettings.qml` renders them without hand-written QML.
- New `home-screen` decoration slot in `HomeScreen.qml` (alongside
  `paper-background` and `prompt-panel`), so verse/weather/clock widgets work.
- `PluginHost` gives plugins cached `fetchJson()` plus settings access instead
  of raw networking; `network` permission + approval are required.
- Agent-authored plugins carry `agentCreated: true` and **cannot be enabled
  until the user clicks Approve in Settings** — `approvePlugin()` is deliberately
  absent from `ToolSchema::pluginDefinitions()` and from `/plugin-tool`; the
  approval panel shows the agent-written source first.
- `ToolSchema::pluginDefinitions()` exposes only create/write/validate/rescan.
  Agent tools may not overwrite bundled or already-approved/enabled plugins.
  Rollback snapshots are taken before file writes.
- `tests/tst_plugins.cpp` and `tests/tst_ai.cpp` cover creation, validation,
  approval, traversal rejection, rollback, declarative settings and tool calls.

## AI providers and models

Provider support is **core, data-driven C++** (`src/ai/ProviderRegistry.cpp`),
not a plugin — provider logic must also run in the headless daemon, which has
no QML runtime. The same registry is exposed to QML as `providerManager` and to
the HTTP API.

- Definitions ship in code: generic OpenAI-compatible (`openrouter`, `openai`,
  `groq`, `together`, `custom`), native `ollama`, `lm-studio`, and `anthropic`.
  Adding a provider is a registry-data change, not a new protocol.
- `connectProvider(id, key, baseUrl)` validates by listing models; on success
  every discovered model is **enabled by default** and the key is stored in
  `SecretStore`. `disconnectProvider()` removes the key but keeps the toggles.
- Local servers are probed at startup (`detectLocalProviders()`) and appear in
  the same settings list with a "Local" badge — no key needed. A refresh button
  re-probes them.
- Only models that are both **enabled and connected** appear in
  `enabledModels()`, which is what the prompt panel shows. Its old hardcoded
  model list has been removed.
- API keys live in `SecretStore`: env override (`TEMPLNOTE_SECRET_<ID>`), then
  OS keychain (libsecret / Keychain), then a 0600 ChaCha20+HMAC encrypted file
  for headless nodes. Keys never go into plugin settings JSON.
- `ModelRouter` now resolves an explicit `ModelRef` through the registry and
  refuses to run when no provider is connected ("Connect a provider before
  using AI"). It understands Ollama, OpenAI-compatible and Anthropic payloads.
- Settings UI: `qml/settings/AISettings.qml` (new core "AI Providers" section).
  Control API: `GET /providers`, `GET /models`, `POST /provider/connect`,
  `POST /provider/disconnect`, `POST /model/toggle`, `POST /model/default`.
- Tests: `tests/tst_providers.cpp` (mock HTTP server: connect, discovery,
  toggles, persistence, local detection, failure, router routing) and
  `tests/tst_aisettings.cpp` (QML smoke test with a connected provider).

## Tests and performance

12 CTest targets pass, including 4 new headless suites (engine, CRDT, store,
AI). ASAN/UBSAN runs show no memory errors in the new code (Qt's own global
allocation caches are the only leak reports). Benchmark (`benchmark_engine`):

```
engine apply x500: 0.006 ms/op
crdt apply x300:   0.010 ms/op   (was 0.293 ms/op before incremental apply)
crdt merge 200 ops: <1 ms
```
