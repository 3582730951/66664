use rust_audit::{format_line, AnalysisEventRecord};
use std::fs::File;

fn main() {
    let path = std::env::args().nth(1).expect("record fixture path");
    let input = File::open(path).expect("open fixture");
    let record: AnalysisEventRecord = serde_json::from_reader(input).expect("parse fixture");
    println!("{}", format_line(&record));
}
