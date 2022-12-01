//! # mcl-rs
//! This project hosts the high-level wrappers of the mcl rust bindings.
//! 
//! ## Summary
//! This crate provides high-level, rust-friendly bindings for mcl. The purpose of these bindings are
//! to expose a user-friendlier API to what the low-level libmcl-sys API offers. It provides wrappers
//! for all mcl public functions and tries to provide safety at compilation type, however,
//! because of the nature of the library counting on a C project there are cases that it's only possible
//! to catch errors at runtime.


mod low_level;
mod device;
pub use device::*;
mod prog;
pub use prog::*;
mod mcl;
pub use mcl::*;
mod task;
pub use task::*;
mod transfer;
pub use transfer::*;




// Tests

// #[cfg(test)]
// mod tests {
// use super::*;

// #[test]
// fn test_mcl_init_finit() {
//     init(1,0);
//     finit();
// }

// #[test]
// fn test_task_init() {
//     init(1,0);
//     let hdl = task_init("./src/test.cl", "testCL", 1, "", 0);
//     finit();
// }

// #[test]
// fn test_task_set_kernel() {
//     init(1,0);
//     let hdl = task_create();
//     task_set_kernel(&hdl, "./src/test.cl", "testCL", 1, "", 0);
//     finit();
// }

// #[test]
// fn test_task_arg() {
//     let mut array: Vec::<i32> = vec![0, 1, 2, 3];
//     let mut pes = [1;3];
//     let mut les = [1;3];
//     let mut len = array.len();
//     init(1,0);
//     let hdl = task_create();
//     task_set_kernel(&hdl, "./src/test.cl", "testCL", 2, "", 0);
//     task_set_arg(&hdl, 0, &mut array[..],ArgOpt::BUFFER| ArgOpt::Input); 
//     task_set_arg(&hdl, 1, slice::from_mut(& mut len),ArgOpt::SCALAR| ArgOpt::Input); 
//     exec(&hdl, &mut pes, &mut les, DevType::GPU);
//     wait(&hdl);
//     finit();
// }

// #[test]
// fn test_wait_all() {
//     let mut array: Vec::<i32> = vec![0, 1, 2, 3];
//     let mut pes = [1;3];
//     let mut les = [1;3];
//     let mut len = array.len();
//     init(1,0);
//     let hdl = task_create();
//     task_set_kernel(&hdl, "./src/test.cl", "testCL", 2, "", 0);
//     task_set_arg(&hdl, 0, &mut array[..],ArgOpt::BUFFER| ArgOpt::Input); 
//     task_set_arg(&hdl, 1, slice::from_mut(& mut len),ArgOpt::SCALAR| ArgOpt::Input); 
//     exec(&hdl, &mut pes, &mut les, DevType::GPU);
//     wait_all();
//     task_free(&hdl);
//     finit();
// }

// #[test]
// fn test_test() {
//     let mut array: Vec::<i32> = vec![0, 1, 2, 3];
//     let mut pes = [1;3];
//     let mut les = [1;3];
//     let mut len = array.len();
//     init(1,0);
//     let hdl = task_create();
//     task_set_kernel(&hdl, "./src/test.cl", "testCL", 2, "", 0);
//     task_set_arg(&hdl, 0, &mut array[..], (MCL_ARG_BUFFER| MCL_ARG_INPUT).into()); 
//     task_set_arg(&hdl, 1, slice::from_mut(&mut len), (MCL_ARG_SCALAR| MCL_ARG_INPUT).into()); 
//     exec(&hdl, &mut pes, &mut les, DevType::GPU);
//     loop {
//         match test(&hdl) {
//             ReqStatus::Completed => break,
//             _                    => {},
//         }
//     }
//     wait_all();
//     task_free(&hdl);
//     finit();
// }

// #[test]
// fn test_null() {
//     let mut array: Vec::<i32> = vec![0, 1, 2, 3];
//     let mut pes = [1;3];
//     let mut les = [1;3];
//     let mut len = array.len();
//     init(1,0);
//     let hdl = task_create();
//     task_set_kernel(&hdl, "./src/test.cl", "testCL", 2, "", 0);
//     task_set_arg(&hdl, 0, &mut array[..], (MCL_ARG_BUFFER| MCL_ARG_INPUT).into()); 
//     task_set_arg(&hdl, 1, slice::from_mut(&mut len), (MCL_ARG_SCALAR| MCL_ARG_INPUT).into()); 
//     // exec(&hdl, &mut pes, &mut les, MCL_TASK_GPU.into());
//     null(&hdl);
//     assert!(
//         match test(&hdl) {
//             ReqStatus::Completed => true,
//             _                    => false,
//     });
//     task_free(&hdl);
//     finit();
// }

// #[test]
// fn test_create_transfer() {
//     init(1, 0);
//     let t_hdl = transfer_create(2, 2);
//     transfer_free(&t_hdl);
//     finit();
// }

// #[test]
// fn test_transfer_arg() {
//     init(1, 0);
//     let mut array: Vec::<i32> = vec![0, 1, 2, 3];
//     let t_hdl = transfer_create(1, 2);
//     transfer_set_arg(&t_hdl, 0, &mut array[..], (MCL_ARG_INPUT| MCL_ARG_BUFFER| MCL_ARG_RESIDENT ).into());
//     transfer_free(&t_hdl);
//     finit();
// }

// #[test]
// fn test_transfer_wait() {
//     init(1, 0);
//     let mut array: Vec::<i32> = vec![0, 1, 2, 3];
//     let t_hdl = transfer_create(1, 2);
//     transfer_set_arg(&t_hdl, 0, &mut array[..], (MCL_ARG_INPUT| MCL_ARG_BUFFER| MCL_ARG_RESIDENT ).into());
//     transfer_exec(&t_hdl, MCL_TASK_GPU.into());
//     transfer_wait(&t_hdl);
//     transfer_free(&t_hdl);
//     finit();
// }

// #[test]
// fn test_transfer_test() {
//     init(1, 0);
//     let mut array: Vec::<i32> = vec![0, 1, 2, 3];
//     let t_hdl = transfer_create(1, 2);
//     transfer_set_arg(&t_hdl, 0, &mut array[..], (MCL_ARG_INPUT| MCL_ARG_BUFFER| MCL_ARG_RESIDENT ).into());
//     transfer_exec(&t_hdl, MCL_TASK_GPU.into());
//     loop {
//         if transfer_test(&t_hdl) == (MCL_REQ_COMPLETED as i32) {
//             break;
//         }
//     }
    
//     transfer_free(&t_hdl);
//     finit();
// }
// }