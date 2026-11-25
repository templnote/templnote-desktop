# Headless engine architecture

This document describes the reusable engine, store and sync layers shared by
the desktop app and the `templnote-headless` CLI. The QML editor is the
renderer; an `EditorSession` is the authoritative document service in both.

## Layers

```
templnote_domain   QtCore-only: Block, Document, AppPaths
templnote_engine   QtCore-only: Commands, Ops, Patches, EditorSession,
                   JsonDocumentState, CrdtDocumentState, ChangeFeed,
                   CapabilityGate
templnote_store    QtCore+Sql:  DocumentStore (CAS writes, journal, snapshots)
templnote_sync     QtCore+Net:  SyncManager (LAN CRDT op exchange)
templnote_core     GUI/QML code (BlockModel, LiveBridge, search, settings)
```

## Engine

`src/engine/Types.h` defines `Actor`, `Selection`, `Op`, `Command`, `Patch` and
`Result<T>`. Every edit is a typed `Command`; every change is an actor-tagged
`Op` inside a `Patch`. Commands and patches are JSON-serializable.

`EditorSession` is the transaction boundary: it stamps causality, emits
`patchApplied`, tracks selection and provides bounded undo/redo with a
pluggable state backend. `JsonDocumentState` wraps the original `Document`;
`CrdtDocumentState` supports multi-user op-set merging. `ChangeFeed` records
the last 500 patches. `CapabilityGate` gates mutations by actor role.

## Store

`Store::DocumentStore` handles revisioned `.templnote` files (`tnRevision`,
`tnLamport`), `QLockFile` + compare-and-swap writes, `QSaveFile` atomic writes,
a SQLite journal of every patch and a snapshot per revision. `restore(id)`
writes an older snapshot as a new revision.

CLI: `templnote-headless apply|script|versions`.

## Sync

`Sync::SyncManager` exchanges CRDT op sets over the LAN with token auth. It is
used by the desktop app; `templnote-headless` links the engine/store layers.

## GUI bridge

`Bridge::LiveBridge` owns an `EditorSession` and `ChangeFeed`; each patch is
pushed into the open `BlockModel`, which applies snapshots through
`applyRemoteDocument()` without entering local undo history.

## Tests

`ctest --test-dir build --output-on-failure` runs the engine, CRDT, store,
model, search and plugin suites. QML suites require an offscreen Qt Quick
backend (see `.github/workflows/ci.yml`).
