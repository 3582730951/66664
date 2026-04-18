pub fn vm1_status() -> &'static str {
    "vm1_rust_facade_ready"
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vm1_facade_reports_ready() {
        assert_eq!(vm1_status(), "vm1_rust_facade_ready");
    }
}
