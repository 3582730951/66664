pub fn vm2_status() -> &'static str {
    "vm2_rust_facade_ready"
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vm2_facade_reports_ready() {
        assert_eq!(vm2_status(), "vm2_rust_facade_ready");
    }
}
