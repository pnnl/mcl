use bitflags::bitflags;
use libmcl_sys::*;
#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
use std::ffi::c_char;
use std::ffi::{c_void, CStr, CString};

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
#[allow(depricated)]
use std::os::unix::raw::pid_t;
use std::ptr::null_mut;
use std::slice;

use crate::prog::PrgType;
use crate::registered_buffer::RegisteredBuffer;
#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
use crate::registered_buffer::SharedMemBuffer;

#[derive(Clone, PartialEq)]
pub(crate) enum ReqStatus {
    Completed,
    Allocated,
    Pending,
    InProgress,
    Finishing,
    Unknown,
}
// pub struct ArgOpt;
bitflags! {
    pub(crate) struct ArgOpt: u64 {
        const EMPTY = 0 as u64;
        const INPUT = MCL_ARG_INPUT as u64;
        const OUTPUT = MCL_ARG_OUTPUT as u64;
        const SCALAR = MCL_ARG_SCALAR as u64;
        const BUFFER = MCL_ARG_BUFFER as u64;
        const RESIDENT = MCL_ARG_RESIDENT as u64;
        const INVALID = MCL_ARG_INVALID as u64;
        const RDONLY = MCL_ARG_RDONLY as u64;
        const WRONLY = MCL_ARG_WRONLY as u64;
        const LOCAL = MCL_ARG_LOCAL as u64;
        const DONE = MCL_ARG_DONE as u64;
        const DYNAMIC = MCL_ARG_DYNAMIC as u64;
        const REWRITE = MCL_ARG_REWRITE as u64;
        #[cfg(feature="shared_mem")]
        const SHARED = MCL_ARG_SHARED as u64;
        #[cfg(feature="shared_mem")]
        const SHARED_MEM_NEW = MCL_SHARED_MEM_NEW as u64;
        #[cfg(feature="shared_mem")]
        const SHARED_MEM_DEL_OLD = MCL_SHARED_MEM_DEL_OLD as u64;
    }

    pub(crate) struct TaskOpt: u64 {
        const EMPTY = 0 as u64;
        const SHARED = MCL_HDL_SHARED as u64;
    }
}

/// Initializes MCL
///
/// ## Arguments
///
/// * `workers` - The number of workers to use
/// * `flags` - Flags to pass to MCL
pub(crate) fn init(workers: u64, flags: u64) {
    unsafe {
        let err = mcl_init(workers, flags);
        if err != 0 {
            panic!("Error {}. Could not initialize MCL", err);
            // Free memory and panic (?)
        }
    }
}

/// Finalizes MCL
pub(crate) fn finit() {
    unsafe {
        let err = mcl_finit();
        if err != 0 {
            panic!("Error {}. Could not finalize MCL", err);
            // Free memory and panic (?)
        }
    }
}

/// Get the info of a specific device
///
/// ## Arguments
///
/// * `id` - The ID of the device to retrieve info for
///
/// Returns the info of specificed device
pub(crate) fn get_dev(id: u32) -> crate::DevInfo {
    let mut dev = mcl_device_info {
        id: 0,
        name: [0; 256],
        vendor: [0; 256],
        type_: 0,
        status: 0,
        mem_size: 0,
        pes: 0,
        ndims: 0,
        wgsize: 0,
        wisize: null_mut(),
    };

    unsafe {
        let err = mcl_get_dev(id, &mut dev);
        if err != 0 {
            // Free memory and panic (?)
            // mcl_finit();
            panic!("Error {}. Could not retrieve device info.", err);
        }
        assert!(!dev.wisize.is_null());
        assert!(dev.ndims > 0);
        let wisize = slice::from_raw_parts(dev.wisize as *const usize, dev.ndims as usize).to_vec();

        crate::DevInfo {
            id: dev.id,
            name: CStr::from_ptr(dev.name.as_ptr())
                .to_string_lossy()
                .to_string(),
            vendor: CStr::from_ptr(dev.vendor.as_ptr())
                .to_string_lossy()
                .to_string(),
            class: dev.type_,
            status: dev.status,
            mem_size: dev.mem_size,
            pes: dev.pes,
            ndims: dev.ndims,
            wgsize: dev.wgsize,
            wisize: wisize,
        }
    }
}

/// Get the number of devices in the system
///
/// Returns the number of devices available
pub(crate) fn get_ndev() -> u32 {
    return unsafe { mcl_get_ndev() };
}

/// Initialize a task to run the specified kernel
///
/// ## Arguments
///
/// `path` - Path to the OpenCL file that include the kernel to run
/// `compile_args` - Compilation args to compile the kernel with
/// `flags` - 0 or MCL_FLAG_NO_RES
pub(crate) fn prg_load(path: &str, compile_args: &str, flags: PrgType) {
    let flag = match flags {
        // PrgType::NONE => MCL_PRG_NONE,
        PrgType::Src => MCL_PRG_SRC,
        PrgType::Ir => MCL_PRG_IR,
        PrgType::Bin => MCL_PRG_BIN,
        PrgType::Graph => MCL_PRG_GRAPH,
        // PrgType::MASK => MCL_PRG_MASK,
        // PrgType::NORES => MCL_FLAG_NO_RES,
    };
    unsafe {
        mcl_prg_load(
            CString::new(path).unwrap().into_raw(),
            CString::new(compile_args).unwrap().into_raw(),
            flag.into(),
        );
    }
}

/// Create a new MCL task
///
/// Returns a new task handle
pub(crate) fn task_create(flags: TaskOpt) -> *mut mcl_handle {
    let hdl = unsafe { mcl_task_create_with_props(flags.bits()) };

    if hdl.is_null() {
        panic!("Error. Could not create MCL task");
    }

    hdl
}

/// Create and initialize a new MCL task
///
/// ## Arguments
///
/// `path` - Path to the OpenCL file that include the kernel to run
/// `name` - The OpenCL kernel to run
/// `num_args` - The number of arguments the kernel accepts
/// `compile_args` - Compilation args to compile the kernel with
/// `flags` - 0 or MCL_FLAG_NO_RES
pub(crate) fn _task_init(
    path: &str,
    name: &str,
    num_args: u64,
    compile_args: &str,
    flags: u64,
) -> *mut mcl_handle {
    // TaskHandle {
    unsafe {
        mcl_task_init(
            CString::new(path).unwrap().into_raw(),
            CString::new(name).unwrap().into_raw(),
            num_args,
            CString::new(compile_args).unwrap().into_raw(),
            flags,
        )
    }
    // }
}

/// Initialize a task to run the specified kernel
///
/// ## Arguments
///
/// `hdl` - Handle associated with task
/// `path` - Path to the OpenCL file that include the kernel to run
/// `name` - The OpenCL kernel to run
/// `num_args` - The number of arguments the kernel accepts
/// `compile_args` - Compilation args to compile the kernel with
/// `flags` - 0 or MCL_FLAG_NO_RES
pub(crate) fn task_set_kernel(hdl: *mut mcl_handle, name: &str, num_args: u64) {
    unsafe {
        mcl_task_set_kernel(hdl, CString::new(name).unwrap().into_raw(), num_args);
    }
}

/// Set up an argument associated with a task
///
/// ## Arguments
///
/// `hdl` - Task handle to set argument for
/// `argid` - The index of the argument
/// `array_slice` - The data to pass
/// `flags` - Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
pub(crate) fn task_set_arg(hdl: *mut mcl_handle, argid: u64, arg: &[u8], flags: ArgOpt) {
    // println!("{argid} {:?} {} {flags:?} {:x}",arg.as_ptr(),arg.len(),flags.bits());
    let err = unsafe {
        mcl_task_set_arg(
            hdl,
            argid,
            arg.as_ptr() as *mut c_void,
            arg.len() as u64,
            flags.bits(),
        )
    };
    if err != 0 {
        panic!("Error {}. Could not set argument for TaskHandle", err);
    }
}

// pub(crate) fn task_set_local(hdl: *mut mcl_handle, argid: u64, mem_size: usize, flags: ArgOpt) {
//     let err = unsafe { mcl_task_set_arg(hdl, argid, null_mut(), mem_size as u64, flags.bits()) };
//     if err != 0 {
//         panic!("Error {}. Could not set argument for TaskHandle", err);
//     }
// }
// -- End set arg calls --

/// same as task_set_arg but for mcl buffers
///
/// ## Arguments
///
/// `hdl` - Task handle to set argument for
/// `argid` - The index of the argument
/// `array_slice` - The data to pass
/// `flags` - Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
pub(crate) fn task_set_arg_registered_buffer(
    hdl: *mut mcl_handle,
    argid: u64,
    buffer: &RegisteredBuffer,
) {
    // assert!(flags.contains(ArgOpt::BUFFER), "buffer was not specified using the buffer attribute.");
    // println!("task_set_arg_registered_buffer {argid} {:?} {:?} {:?} {:?}",buffer.base_addr(), buffer.u8_len() as u64, buffer.u8_offset()  as i64, buffer.flags().bits());
    let err = unsafe {
        mcl_task_set_arg_buffer(
            hdl,
            argid,
            buffer.base_addr(),
            buffer.u8_len() as u64,
            buffer.u8_offset() as i64,
            buffer.flags().bits(),
        )
    };
    if err != 0 {
        panic!("Error {}. Could not set argument for TaskHandle", err);
    }
}

/// same as task_set_arg but for mcl shared mem buffers
///
/// ## Arguments
///
/// `hdl` - Task handle to set argument for
/// `argid` - The index of the argument
/// `array_slice` - The data to pass
/// `flags` - Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
pub(crate) fn task_set_arg_shared_mem_buffer(
    hdl: *mut mcl_handle,
    argid: u64,
    buffer: &SharedMemBuffer,
) {
    // assert!(flags.contains(ArgOpt::BUFFER), "buffer was not specified using the buffer attribute.");
    // println!("task_set_arg_registered_buffer {argid} {:?} {:?} {:?} {:?}",buffer.base_addr(), buffer.u8_len() as u64, buffer.u8_offset()  as i64, buffer.flags().bits());
    let err = unsafe {
        mcl_task_set_arg_buffer(
            hdl,
            argid,
            buffer.base_addr(),
            buffer.u8_len() as u64,
            buffer.u8_offset() as i64,
            buffer.flags().bits(),
        )
    };
    if err != 0 {
        panic!("Error {}. Could not set argument for TaskHandle", err);
    }
}

/// Execute a specified task
///
/// ## Arguments
///
/// `hdl` - The handle to execute
/// `pes` - An array of size MCL_DEV_DIMS containing the number of threads in each dimension
/// `les` - An array of size MCL_DEV_DIMS contianing the local work dimensions
/// `type` - Specify compute locations using DevType::* enum
pub(crate) fn exec(
    hdl: *mut mcl_handle,
    pes: &mut [u64; 3],
    les: &mut Option<[u64; 3]>,
    t_type: crate::DevType,
) {
    let les = match les {
        Some(les) => les.as_mut_ptr() as *mut _ as *mut u64,
        None => 0 as *mut u64,
    };
    let flags = match t_type {
        crate::DevType::NONE => MCL_TASK_NONE,
        crate::DevType::CPU => MCL_TASK_CPU,
        crate::DevType::GPU => MCL_TASK_GPU,
        crate::DevType::FPGA => MCL_TASK_FPGA,
        crate::DevType::DFT => MCL_TASK_DFT_FLAGS,
        crate::DevType::ANY => MCL_TASK_ANY,
    };
    let err = unsafe {
        mcl_exec(
            hdl,
            pes.as_mut_ptr() as *mut _ as *mut u64,
            les,
            flags.into(),
        )
    };
    if err != 0 {
        panic!("Error {}. Could not execute task.", err);
    }
}

// /// Complete the task without executing  (i.e. trigger dependencies)
// ///
// /// ## Arguments
// ///
// /// * `hdl` - The task handle to complete
// pub(crate) fn null(hdl: *mut mcl_handle) {
//     let err = unsafe { mcl_null(hdl) };
//     if err != 0 {
//         panic!("Error {}. Null task failed", err);
//     }
// }

/// Frees data associated with the task handle
///
/// ## Arguments
///
/// * `hdl` - The handle to remove data for
pub(crate) fn task_free(hdl: *mut mcl_handle) {
    let err = unsafe { mcl_hdl_free(hdl) };
    if err != 0 {
        panic!("Error {}. Could not free task handle", err);
    }
}

/// Test whether a task has completed
///
/// ## Arguments
///
/// hdl - Reference to the task handle to test
///
/// Returns the status of the handle. One of the MCL_REQ_* constants
pub(crate) fn test(hdl: *mut mcl_handle) -> ReqStatus {
    let req_status = unsafe { mcl_test(hdl) } as u32;

    match req_status {
        MCL_REQ_COMPLETED => ReqStatus::Completed,
        MCL_REQ_ALLOCATED => ReqStatus::Allocated,
        MCL_REQ_PENDING => ReqStatus::Pending,
        MCL_REQ_INPROGRESS => ReqStatus::InProgress,
        MCL_REQ_FINISHING => ReqStatus::Finishing,
        _ => ReqStatus::Unknown,
    }
}

/// Create a new transfer request
///
/// ## Arguments
///
/// * `nargs` - Number of arguments to transfer
/// * `ncopies` - How many copies to create
/// * `flags` - Flags to pass to the transfer creation function
///
/// Returns a new transfer handle
pub(crate) fn transfer_create(nargs: u64, ncopies: u64, flags: u64) -> *mut mcl_transfer {
    let transfer_hdl = unsafe { mcl_transfer_create(nargs, ncopies, flags) };
    if transfer_hdl.is_null() {
        panic!("Could not create transfer TaskHandle");
    }

    // TransferHandle {
    transfer_hdl
    // }
}

pub(crate) fn transfer_set_arg(
    t_hdl: *mut mcl_transfer,
    idx: u64,
    arg: &[u8],
    offset: isize,
    flags: ArgOpt,
) {
    let err = unsafe {
        mcl_transfer_set_arg(
            t_hdl,
            idx,
            arg.as_ptr() as *mut c_void,
            arg.len() as u64,
            offset as i64,
            flags.bits(),
        )
    };
    if err != 0 {
        panic!("Error {}. Could not set transfer argument", err);
    }
}

// pub(crate) fn transfer_set_local(
//     t_hdl: *mut mcl_transfer,
//     idx: u64,
//     size: usize,
//     offset: isize,
//     flags: ArgOpt,
// ) {
//     let err = unsafe {
//         mcl_transfer_set_arg(
//             t_hdl,
//             idx,
//             null_mut(),
//             size as u64,
//             offset as i64,
//             flags.bits(),
//         )
//     };
//     if err != 0 {
//         panic!("Error {}. Could not set transfer argument", err);
//     }
// }
// -- end transfer set arg calls --
/// Executes a transfer. Asychronously moves data
///
/// ## Arguments
///
/// * `t_hdl` - Transfer handle to execute
/// * `flags` - Flags to specify devices, same as exec(...)
pub(crate) fn transfer_exec(t_hdl: *mut mcl_transfer, d_type: crate::DevType) {
    let flags = match d_type {
        crate::DevType::NONE => MCL_TASK_NONE,
        crate::DevType::CPU => MCL_TASK_CPU,
        crate::DevType::GPU => MCL_TASK_GPU,
        crate::DevType::FPGA => MCL_TASK_FPGA,
        crate::DevType::DFT => MCL_TASK_DFT_FLAGS,
        crate::DevType::ANY => MCL_TASK_ANY,
    };

    let err = unsafe { mcl_transfer_exec(t_hdl, flags as u64) };
    if err != 0 {
        panic!("Error {}. Transfer exec failed", err);
    }
}

/// Test whether a transfer has completed
///
/// ## Arguments
///
/// * t_hdl - The transfer handle to test
///
/// Returns the status of the handle. One of the MCL_REQ_* constants
pub(crate) fn transfer_test(t_hdl: *mut mcl_transfer) -> ReqStatus {
    let req_status = unsafe { mcl_transfer_test(t_hdl) } as u32;
    match req_status {
        MCL_REQ_COMPLETED => ReqStatus::Completed,
        MCL_REQ_ALLOCATED => ReqStatus::Allocated,
        MCL_REQ_PENDING => ReqStatus::Pending,
        MCL_REQ_INPROGRESS => ReqStatus::InProgress,
        MCL_REQ_FINISHING => ReqStatus::Finishing,
        _ => ReqStatus::Unknown,
    }
}

/// Frees data associated with the transfer handle
///
/// ## Arguments
///
/// * `t_hdl` - The handle to remove data for
pub(crate) fn transfer_free(t_hdl: *mut mcl_transfer) {
    let err = unsafe { mcl_transfer_free(t_hdl) };
    if err != 0 {
        panic!("Error {}. Could not free transfer handle", err);
    }
}

/// Register a  buffer for future use with MCL resident memory
pub(crate) fn register_buffer(arg: &[u8], flags: ArgOpt) {
    assert!(
        flags.contains(ArgOpt::BUFFER),
        "buffer was not specified using the buffer attribute."
    );
    assert!(
        flags.contains(ArgOpt::RESIDENT),
        "buffer was not specified using the resident attribute."
    );
    // println!("register buffer: {:?} {:?} {flags:?}",arg.as_ptr(),arg.len());
    let err =
        unsafe { mcl_register_buffer(arg.as_ptr() as *mut c_void, arg.len() as u64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not register mut buffer", err);
    }
}

/// Unregisters a buffer from MCL Resident memory.
pub(crate) fn unregister_buffer(arg: &[u8]) {
    let err = unsafe { mcl_unregister_buffer(arg.as_ptr() as *mut c_void) };
    if err != 0 {
        panic!("Error {}. Could not inregister  buffer", err);
    }
}

/// Invalidates device allocations.
pub(crate) fn invalidate_buffer(arg: &[u8]) {
    let err = unsafe { mcl_invalidate_buffer(arg.as_ptr() as *mut c_void) };
    if err != 0 {
        panic!("Error {}. Could not inregister  buffer", err);
    }
}

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
pub(crate) fn get_shared_task_id(hdl: *mut mcl_handle) -> Option<u32> {
    Some(unsafe { mcl_task_get_sharing_id(hdl) })
}

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
pub(crate) fn shared_task_test(pid: i32, hdl_id: u32) -> ReqStatus {
    let req_status = unsafe { mcl_test_shared_hdl(pid as pid_t, hdl_id) } as i32;
    if req_status < 0 {
        ReqStatus::Unknown
    } else {
        match req_status as u32 {
            MCL_REQ_COMPLETED => ReqStatus::Completed,
            MCL_REQ_ALLOCATED => ReqStatus::Allocated,
            MCL_REQ_PENDING => ReqStatus::Pending,
            MCL_REQ_INPROGRESS => ReqStatus::InProgress,
            MCL_REQ_FINISHING => ReqStatus::Finishing,
            _ => ReqStatus::Unknown,
        }
    }
}

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
pub(crate) fn get_shared_buffer(name: &str, size: usize, flags: ArgOpt) -> *mut c_void {
    // println!("{:?} {:?} {:?}",flags,flags.bits(),flags.bits() as i32);
    let addr = unsafe {
        mcl_get_shared_buffer(
            name.as_ptr() as *const c_char,
            size as u64,
            flags.bits() as i32,
        )
    };
    assert!(!addr.is_null(), "Getting shared buffer failed {addr:?}");
    addr
}

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
pub(crate) fn detach_shared_buffer(addr: *mut c_void) {
    unsafe { mcl_free_shared_buffer(addr) }
}
