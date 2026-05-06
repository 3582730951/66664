# runtime/trampoline

Token-entry trampoline runtime for Subtask 25 / 26.

## What this module provides
- `TokenEntry` / `TokenManager`
  - HKDF-SHA256 token derivation from `key_context_id + function address (+ symbol name)`
  - deterministic deduped registration of protected entrypoints
- `generate_trampoline(...)`
  - byte generation for `x86`, `x64`, `arm`, `arm64`
  - emits the requested 3-instruction token-entry stub shape for each ISA
- `StackFunctionTable`
  - materializes `token -> relocated function address` records on the current stack
  - protects the materialized table with HMAC-SHA256
  - emits `invalid_token_access` on misses and `stack_function_table_tamper` on HMAC failure
  - routes both through `audit_then_delayed_exit`
- `Dispatcher`
  - validates the stack table and resolves tokens to relocated code addresses
- `TrampolineBundle`
  - serializes relocation metadata plus relocated code bytes for rewriter / loader handoff

## Current integration points
- `backends/rewriter`
  - ELF trampoline injection now writes `.vmptrmp` and patches targeted function heads with token trampolines
  - PE / Mach-O emit trampoline descriptor metadata sections alongside the existing VM thunk metadata
- `tools/vmp-trampoline-inject`
  - standalone CLI for applying trampoline injection from Policy IR JSON
- `tests/runtime_trampoline`
  - HKDF token derivation
  - x86/x64/ARM/ARM64 trampoline byte generation
  - HMAC + audit behavior for stack-resident lookup tables
  - bundle round-trip
  - end-to-end CLI / ELF injection regression

## Notes
- The stack table is materialized on demand inside the dispatcher call path instead of being kept as static data.
- The relocated-code bundle format is intentionally local to this repository and only used by the current test/runtime harness.
- This module exists only inside the Owner Override #2 scope for subtask 25 / 26.
