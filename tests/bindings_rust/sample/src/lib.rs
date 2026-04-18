use vmp_macros::vm_func;

macro_rules! vm_string {
    ($text:literal) => {
        vmp_macros::vm_string_literal!($text)
    };
}

pub trait SampleTrait {
    fn m(&self) -> i32;
}

#[vm_func]
pub fn add(a: i32, b: i32) -> i32 {
    a + b
}

#[vm_func]
pub async fn fetch() -> &'static str {
    "ok"
}

pub struct Type;

impl SampleTrait for Type {
    #[vm_func]
    fn m(&self) -> i32 {
        7
    }
}

#[vmp_macros::vm_string]
pub const GREETING: &str = "hello";

pub const PLAIN: &str = "plain";

pub fn uses_literal() -> &'static str {
    vm_string!("secret key")
}

pub fn unmarked_fn() -> i32 {
    3
}
