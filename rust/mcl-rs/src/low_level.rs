


use std::slice;
use std::ptr::null_mut;
use libmcl_sys::*;
use std::ffi::{c_void, CString, CStr};
use std::mem::size_of;

use crate::task::ArgOpt;
use crate::prog::PrgType;

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
        name: [0;256],
        vendor: [0;256],
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
            name: CStr::from_ptr(dev.name.as_ptr()).to_string_lossy().to_string(),
            vendor: CStr::from_ptr(dev.vendor.as_ptr()).to_string_lossy().to_string(),
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
    
    return  unsafe {mcl_get_ndev()};
}

/// Initialize a task to run the specified kernel
/// 
/// ## Arguments
/// 
/// `path` - Path to the OpenCL file that include the kernel to run
/// `compile_args` - Compilation args to compile the kernel with
/// `flags` - 0 or MCL_FLAG_NO_RES
pub(crate) fn prg_load(path: &str, compile_args: &str, flags: PrgType ) {
    let flag = match flags {
        // PrgType::NONE => MCL_PRG_NONE,
        PrgType::Src => MCL_PRG_SRC,
        PrgType::Ir => MCL_PRG_IR,
        PrgType::Bin=> MCL_PRG_BIN,
        PrgType::Graph => MCL_PRG_GRAPH,
        // PrgType::MASK => MCL_PRG_MASK,
        // PrgType::NORES => MCL_FLAG_NO_RES,
    };
    unsafe {
        mcl_prg_load( CString::new(path).unwrap().into_raw(), CString::new(compile_args).unwrap().into_raw(), flag.into());
    }
}

/// Create a new MCL task
/// 
/// Returns a new task handle
pub(crate) fn task_create() -> *mut mcl_handle {
    
    let hdl = unsafe { mcl_task_create() };
    
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
pub(crate) fn _task_init(path: &str, name: &str, num_args: u64, compile_args: &str, flags: u64) -> *mut mcl_handle {

    // TaskHandle {
    unsafe { mcl_task_init(CString::new(path).unwrap().into_raw(), CString::new(name).unwrap().into_raw(), num_args,  CString::new(compile_args).unwrap().into_raw(), flags) }
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
pub(crate) fn task_set_kernel(hdl: *mut mcl_handle, name: &str, num_args: u64 ) {
    
    unsafe {
        mcl_task_set_kernel(hdl,  CString::new(name).unwrap().into_raw(), num_args);
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
pub(crate) fn task_set_arg_mut<T>(hdl: *mut mcl_handle, argid: u64, array_slice: &mut [T], flags: ArgOpt) {
    
    let err = unsafe { mcl_task_set_arg(hdl, argid, array_slice.as_mut_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not set argument for TaskHandle", err);
    }
}

pub(crate) fn task_set_arg<T>(hdl: *mut mcl_handle, argid: u64, array_slice: &[T], flags: ArgOpt) {

    let err = unsafe { mcl_task_set_arg(hdl, argid, array_slice.as_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not set argument for TaskHandle", err);
    }

}

pub(crate) fn task_set_local(hdl: *mut mcl_handle, argid: u64, mem_size: usize, flags: ArgOpt) {

    let err = unsafe { mcl_task_set_arg(hdl, argid, null_mut(), mem_size as u64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not set argument for TaskHandle", err);
    }
}
// -- End set arg calls --

/// same as task_set_arg but for mcl buffers
/// 
/// ## Arguments
/// 
/// `hdl` - Task handle to set argument for
/// `argid` - The index of the argument
/// `array_slice` - The data to pass
/// `flags` - Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
pub(crate) fn task_set_arg_buffer_mut<T>(hdl: *mut mcl_handle, argid: u64, array_slice: &mut [T], offset: usize, flags: ArgOpt) {
    assert!(flags.contains(ArgOpt::BUFFER), "buffer was not specified using the buffer attribute.");
    let err = unsafe { mcl_task_set_arg_buffer(hdl, argid, array_slice.as_mut_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, offset  as i64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not set argument for TaskHandle", err);
    }
}

pub(crate) fn task_set_arg_buffer<T>(hdl: *mut mcl_handle, argid: u64, array_slice: &[T], offset: usize, flags: ArgOpt) {
    assert!(flags.contains(ArgOpt::BUFFER), "buffer was not specified using the buffer attribute.");
    let err = unsafe { mcl_task_set_arg_buffer(hdl, argid, array_slice.as_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, offset  as i64, flags.bits()) };
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
pub(crate) fn exec(hdl: *mut mcl_handle, pes: &mut [u64; 3], les: &mut [u64; 3], t_type: crate::DevType) {
    
    let flags =  match t_type {
        crate::DevType::NONE => MCL_TASK_NONE,
        crate::DevType::CPU => MCL_TASK_CPU,
        crate::DevType::GPU => MCL_TASK_GPU,
        crate::DevType::FPGA => MCL_TASK_FPGA,
        crate::DevType::DFT => MCL_TASK_DFT_FLAGS,
        crate::DevType::ANY => MCL_TASK_ANY,
    };
    let err = unsafe { mcl_exec(hdl, pes.as_mut_ptr() as *mut _ as *mut u64, les.as_mut_ptr() as *mut _ as *mut u64, flags.into()) };
    if err != 0 {
        panic!("Error {}. Could not execute task.", err);
    }
}

/// Complete the task without executing  (i.e. trigger dependencies)
/// 
/// ## Arguments
/// 
/// * `hdl` - The task handle to complete
pub(crate) fn null(hdl: *mut mcl_handle) {
    
    let err = unsafe { mcl_null(hdl) };
    if err != 0 {
        panic!("Error {}. Null task failed", err);
    }
}

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

/// Wait for a task to complete
/// 
/// ## Arguments
/// 
/// `hdl` - The handle to wait for
pub(crate) fn wait(hdl: *mut mcl_handle) {
    
    let err = unsafe { mcl_wait(hdl) };
    if err != 0 {
        panic!("Error {}. Could not wait for TaskHandle to complete.", err);
    }
}

/// Wait for all pending tasks to complete
///
pub(crate) fn wait_all(){
    let err = unsafe { mcl_wait_all() };
    if err != 0 {
        panic!("Error {}. Wait all failed.", err);
    }
}

/// Test whether a task has completed
/// 
/// ## Arguments
/// 
/// hdl - Reference to the task handle to test
/// 
/// Returns the status of the handle. One of the MCL_REQ_* constants
pub(crate) fn test(hdl: *mut mcl_handle) -> crate::ReqStatus {
    
    let req_status = unsafe { mcl_test(hdl) } as u32;

    match req_status  {
        MCL_REQ_COMPLETED  => crate::ReqStatus::Completed,    
        MCL_REQ_ALLOCATED  => crate::ReqStatus::Allocated,     
        MCL_REQ_PENDING    => crate::ReqStatus::Pending,     
        MCL_REQ_INPROGRESS => crate::ReqStatus::InProgress,     
        MCL_REQ_FINISHING  => crate::ReqStatus::Finishing, 
        _                  => crate::ReqStatus::Unknown,     
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

/// Set up an argument associated with a transfer handle
/// 
/// # Arguments
/// 
/// * `t_hdl` - Transfer handle to set argument for
/// * `idx` - The index of the argument
/// * `array_slice` - The data to pass
/// * `flags` - Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
pub(crate) fn transfer_set_arg_mut<T>(t_hdl: *mut mcl_transfer , idx: u64, array_slice: &mut [T], offset: isize, flags: ArgOpt) {

    let err = unsafe { mcl_transfer_set_arg(t_hdl, idx, array_slice.as_mut_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, offset as i64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not set transfer argument", err);
    }
}

pub(crate) fn transfer_set_arg<T>(t_hdl: *mut mcl_transfer, idx: u64, array_slice: & [T], offset: isize, flags: ArgOpt) {

    let err = unsafe { mcl_transfer_set_arg(t_hdl, idx, array_slice.as_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, offset as i64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not set transfer argument", err);
    }
}

pub(crate) fn transfer_set_local(t_hdl: *mut mcl_transfer, idx: u64, size: usize, offset: isize, flags: ArgOpt) {

    let err = unsafe { mcl_transfer_set_arg(t_hdl, idx, null_mut(), size as u64, offset as i64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not set transfer argument", err);
    }
}
// -- end transfer set arg calls --
/// Executes a transfer. Asychronously moves data
/// 
/// ## Arguments
/// 
/// * `t_hdl` - Transfer handle to execute
/// * `flags` - Flags to specify devices, same as exec(...)
pub(crate) fn transfer_exec(t_hdl: *mut mcl_transfer, d_type: crate::DevType) {

    let flags =  match d_type {
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

/// Wait for a transfer to complete
/// 
/// ## Arguments
/// 
/// `t_hdl` - The tranfser handle to wait for
pub(crate) fn transfer_wait(t_hdl: *mut mcl_transfer) {

    let err = unsafe { mcl_transfer_wait(t_hdl) };
    if err != 0 {
        panic!("Error {}. Failed waiting on transfer TaskHandle", err);
    }
}

/// Test whether a transfer has completed
/// 
/// ## Arguments
/// 
/// * t_hdl - The transfer handle to test
/// 
/// Returns the status of the handle. One of the MCL_REQ_* constants
pub(crate) fn transfer_test(t_hdl: *mut mcl_transfer) -> i32 {
    
    let err = unsafe { mcl_transfer_test(t_hdl) };
    return err;
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

/// Register a mut buffer for future use with MCL resident memory
pub(crate) fn register_buffer_mut<T>( array_slice: &mut [T],  flags: ArgOpt){
    assert!(flags.contains(ArgOpt::BUFFER), "buffer was not specified using the buffer attribute.");
    assert!(flags.contains(ArgOpt::RESIDENT), "buffer was not specified using the resident attribute.");
    let err = unsafe { mcl_register_buffer(array_slice.as_mut_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not register mut buffer", err);
    }
}

/// Register a buffer for future use with MCL resident memory
pub(crate) fn register_buffer<T>( array_slice: &[T],  flags: ArgOpt){
    assert!(flags.contains(ArgOpt::BUFFER), "buffer was not specified using the buffer attribute.");
    assert!(flags.contains(ArgOpt::RESIDENT), "buffer was not specified using the resident attribute.");
    let err = unsafe { mcl_register_buffer(array_slice.as_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, flags.bits()) };
    if err != 0 {
        panic!("Error {}. Could not register  buffer", err);
    }
}

/// Unregisters a buffer from MCL Resident memory.
pub(crate) fn unregister_buffer<T>( array_slice: &[T]){
    let err = unsafe { mcl_unregister_buffer(array_slice.as_ptr() as *mut c_void) };
    if err != 0 {
        panic!("Error {}. Could not inregister  buffer", err);
    }
}

/// Invalidates device allocations.
pub(crate) fn invalidate_buffer<T>( array_slice: &[T]){
    let err = unsafe { mcl_invalidate_buffer(array_slice.as_ptr() as *mut c_void) };
    if err != 0 {
        panic!("Error {}. Could not inregister  buffer", err);
    }
}







