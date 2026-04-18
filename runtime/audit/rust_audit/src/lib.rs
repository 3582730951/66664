use serde::{Deserialize, Serialize};
use std::fs::{create_dir_all, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::Duration;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AnalysisEventRecord {
    pub event_date: String,
    pub event_time: String,
    pub thread_id: u64,
    pub event_type: String,
    pub program_counter: u64,
    pub module_name: String,
    pub symbol_name: String,
    pub symbol_offset: i64,
    pub arch: String,
    pub platform: String,
    pub process_id: u64,
    pub context_note: String,
}

pub fn format_line(record: &AnalysisEventRecord) -> String {
    format!(
        "[{}] [{}] [{}] [{}] [pid={}] [tid={}] [{}] [pc=0x{:x}] [module={}] [symbol={}] [offset={}] [note={}]",
        record.event_date,
        record.event_time,
        record.platform,
        record.arch,
        record.process_id,
        record.thread_id,
        record.event_type,
        record.program_counter,
        record.module_name,
        record.symbol_name,
        format_offset(record.symbol_offset),
        escape_note(&record.context_note)
    )
}

fn format_offset(offset: i64) -> String {
    let sign = if offset < 0 { '-' } else { '+' };
    let magnitude = offset.unsigned_abs();
    format!("{}0x{:x}", sign, magnitude)
}

fn escape_note(note: &str) -> String {
    let mut out = String::with_capacity(note.len());
    for ch in note.chars() {
        match ch {
            ']' => out.push_str("\\x5D"),
            '\n' => out.push_str("\\n"),
            '\r' => {}
            _ => out.push(ch),
        }
    }
    out
}

#[derive(Debug, Clone)]
pub struct AuditWriter {
    inner: Arc<Mutex<Option<File>>>,
}

impl AuditWriter {
    pub fn new(path: impl AsRef<Path>) -> Self {
        let path = path.as_ref().to_path_buf();
        if let Some(parent) = path.parent() {
            let _ = create_dir_all(parent);
        }
        let file = OpenOptions::new().create(true).append(true).open(&path).ok();
        Self {
            inner: Arc::new(Mutex::new(file)),
        }
    }

    pub fn append(&self, record: &AnalysisEventRecord) {
        let line = format!("{}\n", format_line(record));
        if let Ok(mut guard) = self.inner.lock() {
            if let Some(file) = guard.as_mut() {
                let _ = file.write_all(line.as_bytes());
            }
        }
    }

    pub fn flush(&self) {
        if let Ok(mut guard) = self.inner.lock() {
            if let Some(file) = guard.as_mut() {
                let _ = file.flush();
                let _ = file.sync_data();
            }
        }
    }

    pub fn default_path() -> PathBuf {
        if let Ok(value) = std::env::var("VMP_AUDIT_LOG_PATH") {
            if !value.is_empty() {
                return PathBuf::from(value);
            }
        }
        #[cfg(target_os = "windows")]
        {
            if let Ok(local_app_data) = std::env::var("LOCALAPPDATA") {
                if !local_app_data.is_empty() {
                    return PathBuf::from(local_app_data).join("vmp").join("vm_runtime_audit.log");
                }
            }
        }
        #[cfg(target_os = "android")]
        {
            if let Ok(files_dir) = std::env::var("VMP_ANDROID_FILES_DIR") {
                if !files_dir.is_empty() {
                    return PathBuf::from(files_dir).join("vm_runtime_audit.log");
                }
            }
        }
        #[cfg(target_os = "ios")]
        {
            if let Ok(documents_dir) = std::env::var("VMP_IOS_DOCUMENTS_DIR") {
                if !documents_dir.is_empty() {
                    return PathBuf::from(documents_dir).join("vm_runtime_audit.log");
                }
            }
        }
        std::env::current_dir()
            .unwrap_or_else(|_| PathBuf::from("."))
            .join("vm_runtime_audit.log")
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReactionPolicy {
    Log,
    Degrade,
    DecoyTerminate,
    AuditOnly,
    AuditThenDelayedExit,
}

type ExitHook = std::sync::Arc<dyn Fn() + Send + Sync>;
type DelaySelector = Box<dyn Fn() -> Duration + Send + Sync>;
type Scheduler = Box<dyn Fn(Duration, ExitHook) + Send + Sync>;

pub struct ReactionDispatcher {
    writer: AuditWriter,
    default_policy: ReactionPolicy,
    exit_hook: ExitHook,
    delay_selector: DelaySelector,
    scheduler: Scheduler,
}

impl ReactionDispatcher {
    pub fn new(writer: AuditWriter, default_policy: ReactionPolicy) -> Self {
        Self {
            writer,
            default_policy,
            exit_hook: std::sync::Arc::new(|| std::process::exit(0)),
            delay_selector: Box::new(default_delay),
            scheduler: Box::new(default_scheduler),
        }
    }

    pub fn dispatch(&self, record: &AnalysisEventRecord) {
        self.dispatch_with_policy(record, self.default_policy);
    }

    pub fn dispatch_with_policy(&self, record: &AnalysisEventRecord, policy: ReactionPolicy) {
        match policy {
            ReactionPolicy::AuditOnly => self.writer.append(record),
            ReactionPolicy::AuditThenDelayedExit => {
                self.writer.append(record);
                let delay = (self.delay_selector)();
                (self.scheduler)(delay, self.exit_hook.clone());
            }
            ReactionPolicy::Log => {
                // TODO(subtask_04): integrate log-state-machine behavior.
            }
            ReactionPolicy::Degrade => {
                // TODO(subtask_04): integrate degrade-state-machine behavior.
            }
            ReactionPolicy::DecoyTerminate => {
                // TODO(subtask_04): integrate decoy-terminate-state-machine behavior.
            }
        }
    }

    pub fn set_exit_hook(&mut self, hook: Box<dyn Fn() + Send + Sync>) {
        self.exit_hook = std::sync::Arc::from(hook);
    }

    pub fn set_delay_selector(&mut self, selector: DelaySelector) {
        self.delay_selector = selector;
    }

    pub fn set_scheduler(&mut self, scheduler: Scheduler) {
        self.scheduler = scheduler;
    }
}

fn default_delay() -> Duration {
    use std::time::{SystemTime, UNIX_EPOCH};
    let seed = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.subsec_nanos())
        .unwrap_or(0);
    Duration::from_millis(1000 + u64::from(seed % 2001))
}

fn default_scheduler(delay: Duration, hook: ExitHook) {
    std::thread::spawn(move || {
        std::thread::sleep(delay);
        hook();
    });
}
