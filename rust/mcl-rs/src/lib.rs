//! # mcl-rs
//! This project hosts the high-level wrappers of the mcl rust bindings.
//!
//! ## Summary
//! This crate provides high-level, rust-friendly bindings for mcl. The purpose of these bindings are
//! to expose a user-friendlier API to what the low-level libmcl-sys API offers. It provides wrappers
//! for all mcl public functions and tries to provide safety at compilation type, however,
//! because of the nature of the library counting on a C project there are cases that it's only possible
//! to catch errors at runtime.

mod device;
mod low_level;
pub use device::*;
mod prog;
pub use prog::*;
mod mcl;
pub use mcl::*;
mod task;
pub use task::*;
mod transfer;
pub use transfer::*;
mod registered_buffer;
pub use registered_buffer::*;
