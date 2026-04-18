pub fn status() -> &'static str {
    "rust_strings_ready"
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strings_facade_reports_ready() {
        assert_eq!(status(), "rust_strings_ready");
    }
}
