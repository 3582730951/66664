use rust_audit::{format_line, AnalysisEventRecord, AuditWriter, ReactionDispatcher, ReactionPolicy};
use std::sync::{Arc, Mutex};
use std::time::Duration;

fn fixture_record() -> AnalysisEventRecord {
    AnalysisEventRecord {
        event_date: "2026-04-18".into(),
        event_time: "12:34:56".into(),
        thread_id: 88,
        event_type: "hw_breakpoint".into(),
        program_counter: 0x1000,
        module_name: "game.dll".into(),
        symbol_name: "verify_score".into(),
        symbol_offset: 0x24,
        arch: "x64".into(),
        platform: "windows".into(),
        process_id: 1234,
        context_note: "detector_reported_debug_register_event".into(),
    }
}

#[test]
fn line_format_exact_rust() {
    let expected = std::fs::read_to_string("/workspace/vmp/tests/audit/golden_line.txt").unwrap();
    assert_eq!(format!("{}\n", format_line(&fixture_record())), expected);
}

#[test]
fn fail_to_open_does_not_panic() {
    let writer = AuditWriter::new("/proc/self/status/audit.log");
    writer.append(&AnalysisEventRecord {
        event_date: "2026-04-18".into(),
        event_time: "00:00:00".into(),
        thread_id: 1,
        event_type: "unknown".into(),
        program_counter: 0,
        module_name: String::new(),
        symbol_name: String::new(),
        symbol_offset: 0,
        arch: "x64".into(),
        platform: "linux".into(),
        process_id: 1,
        context_note: "still_running".into(),
    });
    writer.flush();
}

#[test]
fn reaction_audit_only_rust() {
    let dir = tempfile::tempdir().unwrap();
    let log = dir.path().join("audit.log");
    let writer = AuditWriter::new(&log);
    let mut dispatcher = ReactionDispatcher::new(writer, ReactionPolicy::AuditOnly);
    let exit_count = Arc::new(Mutex::new(0usize));
    let exit_count_clone = Arc::clone(&exit_count);
    dispatcher.set_exit_hook(Box::new(move || {
        *exit_count_clone.lock().unwrap() += 1;
    }));
    dispatcher.set_delay_selector(Box::new(|| Duration::from_secs(1)));
    dispatcher.set_scheduler(Box::new(|_, hook| hook()));
    dispatcher.dispatch(&AnalysisEventRecord {
        event_date: "2026-04-18".into(),
        event_time: "08:09:10".into(),
        thread_id: 2,
        event_type: "integrity_mismatch".into(),
        program_counter: 0x55,
        module_name: "mod".into(),
        symbol_name: "sym".into(),
        symbol_offset: -4,
        arch: "x64".into(),
        platform: "linux".into(),
        process_id: 77,
        context_note: "audit only".into(),
    });
    let lines = std::fs::read_to_string(&log).unwrap();
    assert_eq!(lines.lines().count(), 1);
    assert_eq!(*exit_count.lock().unwrap(), 0);
}

#[test]
fn reaction_audit_then_delayed_exit_rust() {
    let dir = tempfile::tempdir().unwrap();
    let log = dir.path().join("audit.log");
    let writer = AuditWriter::new(&log);
    let mut dispatcher = ReactionDispatcher::new(writer, ReactionPolicy::AuditOnly);
    let exit_count = Arc::new(Mutex::new(0usize));
    let exit_count_clone = Arc::clone(&exit_count);
    dispatcher.set_exit_hook(Box::new(move || {
        *exit_count_clone.lock().unwrap() += 1;
    }));
    let observed_delay = Arc::new(Mutex::new(Duration::from_secs(0)));
    let observed_delay_clone = Arc::clone(&observed_delay);
    dispatcher.set_delay_selector(Box::new(|| Duration::from_secs(2)));
    dispatcher.set_scheduler(Box::new(move |delay, hook| {
        *observed_delay_clone.lock().unwrap() = delay;
        hook();
    }));
    dispatcher.dispatch_with_policy(
        &AnalysisEventRecord {
            event_date: "2026-04-18".into(),
            event_time: "11:22:33".into(),
            thread_id: 91,
            event_type: "hw_breakpoint".into(),
            program_counter: 0x99,
            module_name: "core".into(),
            symbol_name: "sensitive".into(),
            symbol_offset: 8,
            arch: "x64".into(),
            platform: "linux".into(),
            process_id: 90,
            context_note: "owner override".into(),
        },
        ReactionPolicy::AuditThenDelayedExit,
    );
    let lines = std::fs::read_to_string(&log).unwrap();
    assert_eq!(lines.lines().count(), 1);
    assert_eq!(*exit_count.lock().unwrap(), 1);
    assert_eq!(*observed_delay.lock().unwrap(), Duration::from_secs(2));
}

#[test]
fn cross_language_golden() {
    let fixture = "/workspace/vmp/tests/audit/golden_record.json";
    let rust_output = std::process::Command::new("cargo")
        .args(["run", "-q", "-p", "rust_audit", "--example", "format_record", "--", fixture])
        .current_dir("/workspace/vmp")
        .output()
        .unwrap();
    assert!(rust_output.status.success());
    let cpp_output = std::process::Command::new("/workspace/vmp/build-linux-x64/tests/audit_cpp_format")
        .arg(fixture)
        .output()
        .unwrap();
    assert!(cpp_output.status.success());
    assert_eq!(String::from_utf8(rust_output.stdout).unwrap(), String::from_utf8(cpp_output.stdout).unwrap());
}
