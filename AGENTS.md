# AGENTS.md

## Project Identity

This repository owns Cardputer Chess: a complete offline chess game and chess
engine for the M5Stack Cardputer-Adv (ESP32-S3FN8, 8 MB flash, no PSRAM).

It owns the portable chess rules/search library, Cardputer firmware UI,
host-side tests, build configuration, and project documentation. It does not
own M5Stack board-support packages, hardware schematics, or workspace-wide
policy.

## Start Here

- Read this file and `README.md`.
- Read `docs/architecture.md` before changing engine or UI boundaries.
- Run `git status --short --branch` before meaningful edits.

## Work Mode

Classify meaningful work as `Ship`, `Branch`, `Ask`, or `Stop`.

- Use `Ship` for narrow documentation, test, or bug-fix changes.
- Use `Branch` for engine behavior, UI/product work, hardware integration, or
  multi-file features.
- Use `Ask` when the dirty diff is mixed or hardware behavior cannot be safely
  inferred.
- Use `Stop` for secrets, destructive Git, history rewriting, or changes beyond
  this repository's boundary.

## Branching And Git

- Keep `main` buildable and easy to pull.
- Simon explicitly authorized direct `main` development and publication for
  the initial complete application build on 2026-07-19.
- Use the current checkout and a short-lived `feat/`, `fix/`, `docs/`, or
  `spike/` branch for later nontrivial work unless Simon again prefers `main`.
- Check `git status --short --branch` before and after meaningful edits.
- Inspect dirty changes before pulling, committing, or pushing.
- Commit only coherent, reviewed, validated work.
- Do not merge into `main` without Simon's explicit request.
- Do not use reset, stash, clean, force-push, or other destructive Git commands
  unless Simon explicitly asks.

## Validation

Run all checks relevant to a change:

```sh
make test
make test-sanitize
uvx --with pip platformio run
```

`make test` covers legal move generation, complete-rule behavior, perft
positions, evaluation/search smoke tests, and level configuration. The
PlatformIO build proves firmware integration; physical keyboard, LCD, audio,
battery, and sustained thermal behavior still require a Cardputer-Adv.

## Secrets And Runtime State

Never commit credentials, `.env` files, private keys, build output, PlatformIO
caches, logs, firmware dumps, or device-local saved games. Generated files
belong under ignored `.pio/`, `build/`, or other explicitly ignored paths.

## Workspace Memory Bridge

When a containing Simon workspace provides `system/pkm_memory_bridge.md` and
`9_pkm/`:

- Follow the bridge after meaningful project work.
- Read `9_pkm/AGENTS.md` before writing to the vault.
- Keep project and PKM Git changes separate; report pending memory handoffs.

When opened independently, follow this repository guide only.

## Reporting

If work is dirty, incomplete, blocked, unvalidated, hardware-unverified, or
intentionally unpushed, report the affected files, reason, and next safe step.
