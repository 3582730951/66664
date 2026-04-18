# runtime/audit

`runtime/audit/` implements the local structured audit sink for detector-reported analysis events in plan §14/§15, including the owner override for hardware-breakpoint events: `audit_then_delayed_exit`.

## Components

- `AnalysisEventRecord`: shared event payload shape.
- `format_line(record)`: canonical structured one-line text encoder.
- `AuditWriter`: append-only, failure-silent local log writer.
- `IDetector` / `NullDetector`: detector sink interface plus test double.
- `ReactionDispatcher`: audit policy fan-out with `audit_only` and `audit_then_delayed_exit` active in this subtask.
- `vm_placeholder_analysis_awareness_hook()`: empty placeholder hook, called once during library load and callable manually.

## Line format

Each record is encoded as exactly one line:

```text
[YYYY-MM-DD] [HH:MM:SS] [platform] [arch] [pid=X] [tid=Y] [event_type] [pc=0x...] [module=...] [symbol=...] [offset=+0xN|-0xN] [note=...]
```

### EBNF

```ebnf
line        = date, space, time, space, platform, space, arch, space, pid, space, tid,
              space, event_type, space, pc, space, module, space, symbol, space, offset,
              space, note ;
space       = " ";
date        = "[", year, "-", month, "-", day, "]" ;
time        = "[", hour, ":", minute, ":", second, "]" ;
platform    = "[", text, "]" ;
arch        = "[", text, "]" ;
pid         = "[pid=", digits, "]" ;
tid         = "[tid=", digits, "]" ;
event_type  = "[", text, "]" ;
pc          = "[pc=0x", hex, "]" ;
module      = "[module=", text, "]" ;
symbol      = "[symbol=", text, "]" ;
offset      = "[offset=", ("+" | "-"), "0x", hex, "]" ;
note        = "[note=", escaped_text, "]" ;
text        = { any_character_except_line_break } ;
escaped_text= { any_character_except_line_break_and_close_bracket | "\\x5D" | "\\n" } ;
```

### Escaping rules

Only `note` is escaped by the canonical formatter:

- `]` → `\\x5D`
- newline (`\n`) → `\\n`
- carriage return is dropped

Missing symbol/module metadata is still emitted with empty values, for example:

```text
[module=] [symbol=] [offset=+0x0]
```

## Field semantics

- `event_date`: local date string, `YYYY-MM-DD`
- `event_time`: local time string, `HH:MM:SS`
- `thread_id`: OS thread id when available
- `event_type`: detector category such as `hw_breakpoint` or `integrity_mismatch`
- `program_counter`: instruction pointer / program counter, `0` if unknown
- `module_name`: module / image name, empty if unknown
- `symbol_name`: nearest symbol, empty if unknown
- `symbol_offset`: signed offset from symbol, `0` if unknown
- `arch`: `x86 | x64 | arm | arm64`
- `platform`: `windows | linux | android | ios`
- `process_id`: OS process id
- `context_note`: free text; escaped to keep one-line parseability

## Default log paths

- Windows: `%LOCALAPPDATA%/vmp/vm_runtime_audit.log`, else `cwd/vm_runtime_audit.log`
- Linux: `cwd/vm_runtime_audit.log`
- Android: app-writable internal files directory when supplied by integration glue, else `cwd/vm_runtime_audit.log`
- iOS: `NSDocumentDirectory` equivalent when supplied by integration glue, else `cwd/vm_runtime_audit.log`
- Any platform may override with `VMP_AUDIT_LOG_PATH`

## Thread safety and write model

- `AuditWriter` opens the target once in append mode.
- Writes are serialized with an internal mutex.
- Each append emits exactly one formatted line plus trailing newline.
- `flush()` syncs the currently open file descriptor/handle when available.

## Failure model

- Open failure degrades to silent drop mode.
- Append failure is ignored and does not throw.
- Flush failure is ignored and does not throw.
- Detector sink dispatch and delayed-exit scheduling are best-effort and failure-silent.
