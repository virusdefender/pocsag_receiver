use std::fmt::Display;

use crate::RawCodeWord;

pub trait Message: Display + Sized {
    fn name() -> &'static str;
    fn address() -> u32;
    fn max_cw() -> usize;
    fn parse(cw: &[RawCodeWord]) -> Result<Self, String>;
}
