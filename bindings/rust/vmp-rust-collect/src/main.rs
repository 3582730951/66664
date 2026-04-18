use std::path::PathBuf;

fn main() {
    if let Err(err) = run() {
        eprintln!("vmp-rust-collect: {err}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    let mut target_dir = None;
    let mut policy_out = None;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--target-dir" => target_dir = args.next().map(PathBuf::from),
            "--policy-out" => policy_out = args.next().map(PathBuf::from),
            _ => return Err(format!("unknown argument: {arg}")),
        }
    }
    let target_dir = target_dir.ok_or_else(|| "--target-dir is required".to_string())?;
    let policy_out = policy_out.ok_or_else(|| "--policy-out is required".to_string())?;
    let policy = vmp_rust_collect::collect_policy_from_target_dir(&target_dir)?;
    vmp_rust_collect::save_policy(&policy_out, &policy)?;
    println!("vmp-rust-collect OK entries={}", policy.entries.len());
    Ok(())
}
