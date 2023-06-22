use libmcl_sys::MCL_DEV_DIMS;

pub const DEV_DIMS: u32 = MCL_DEV_DIMS;
#[allow(unused_imports)]
use std::ffi::CString;

#[derive(Clone, Copy, serde::Serialize, serde::Deserialize)]
pub enum DevType {
    NONE,
    CPU,
    GPU,
    FPGA,
    DFT,
    ANY,
}

#[derive(Debug)]
/// Represents info of the computing device
pub struct DevInfo {
    /// Device ID
    pub id: u64,
    /// Device name
    pub name: String,
    /// Device vendor
    pub vendor: String,
    /// Device class 0: None, 1: CPU, 2: GPU
    pub class: u64,
    /// Device status
    pub status: u64,
    /// Device total amount of memory available
    pub mem_size: u64,
    /// Device number of processing elements (PEs)
    pub pes: u64,
    /// Device number of dimensions
    pub ndims: u64,
    /// Device workgroup max size
    pub wgsize: u64,
    /// Device max size per dimension
    pub wisize: Vec<usize>,
}
