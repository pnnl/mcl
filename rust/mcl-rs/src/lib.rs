use libmcl_sys::*;

pub const DEV_DIMS :u32 = MCL_DEV_DIMS;
#[allow(unused_imports)]
use std::ffi::{CString};

pub struct ArgType;

impl ArgType {
    pub const SCALAR: u64 = MCL_ARG_SCALAR as u64; 
    pub const BUFFER: u64 = MCL_ARG_BUFFER as u64; 
}

pub struct ArgOpt;


impl ArgOpt {
    pub const INPUT: u64 = MCL_ARG_INPUT as u64; 
    pub const OUTPUT: u64 = MCL_ARG_OUTPUT as u64; 
    pub const RESIDENT: u64 = MCL_ARG_RESIDENT as u64; 
    pub const INVALID: u64 = MCL_ARG_INVALID as u64; 
    pub const RDONLY: u64 = MCL_ARG_RDONLY as u64; 
    pub const WRONLY: u64 = MCL_ARG_WRONLY as u64; 
    pub const LOCAL: u64 = MCL_ARG_LOCAL as u64; 
    pub const DONE: u64 = MCL_ARG_DONE as u64; 
    pub const DYNAMIC: u64 = MCL_ARG_DYNAMIC as u64; 
}

#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub enum DevType {
    NONE,
    CPU,
    GPU,
    FPGA,
    #[cfg(feature="versal")]
    NVDLA,
    #[cfg(feature="versal")]
    SIMDEV,
    ANY,
}

#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub enum KernelFlag {
    NONE,
    #[cfg(feature="versal")]
    SRC,
    #[cfg(feature="versal")]
    BIN,
    #[cfg(feature="versal")]
    SIMDEV,
    NORES,
}

#[derive(Clone, PartialEq)]
pub enum ReqStatus {
    COMPLETED,
    ALLOCATED,
    PENDING,
    INPROGRESS,
    FINISHING,
    UNKNOWN,
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
    pub  mem_size: u64, 
    /// Device number of processing elements (PEs)
    pub pes: u64,
    /// Device number of dimensions
    pub ndims: u64,
    /// Device workgroup max size
    pub wgsize: u64,
    /// Device max size per dimension
    pub wisize: Vec::<usize>
}


pub struct Mcl{
    pub _env: MclEnv
}

impl Mcl{
    /// Creates a new mcl task from the given kernel
    /// 
    /// ## Arguments
    /// 
    /// * `prog_path` - The path to the file where the kernel resides
    /// * `kernel_name` - The kernel name
    /// * `num_args` - The number of arguments of the kernel
    /// 
    /// Returns a new Task that can be compiled to a CompiledTask
    /// 
    /// # Example
    /// 
    ///     let t = Task::from("my_path", "my_kernel", 2);
    /// 
    pub fn task(&self, kernel_name: &str, kernel_name_cl: &str, nargs: usize) -> CompiledTask{
        Task::from( kernel_name, kernel_name_cl, nargs).compile()
    }

    // /// Initialize a task to run the specified kernel
    // /// 
    // /// ## Arguments
    // /// 
    // /// `hdl` - Handle associated with task
    // /// `path` - Path to the OpenCL file that include the kernel to run
    // /// `name` - The OpenCL kernel to run
    // /// `num_args` - The number of arguments the kernel accepts
    // /// `compile_args` - Compilation args to compile the kernel with
    // /// `flags` - 0 or MCL_FLAG_NO_RES
    // pub fn task_set_kernel(hdl: *mut mcl_handle, path: &str, name: &str, num_args: u64, compile_args: &str, flags: crate::KernelFlag ) {
    //     low_level::task_set_kernel(hdl, path, name, num_args, compile_args, flags.into());
    // }


    // /// Initialize a task to run the specified binarykernel
    // /// 
    // /// ## Arguments
    // /// 
    // /// `hdl` - Handle associated with task
    // /// `path` - Path to the binary file that implements the kernel to run
    // /// `num_args` - The number of arguments the kernel accepts
    // #[cfg(feature="versal")]
    // pub fn task_set_binary(hdl: *mut mcl_handle, props: mcl_bin_properties, path: &str) {
    //     low_level::task_set_binary(hdl, props, path);
    // }

    // /// Set up an argument associated with a task
    // /// 
    // /// ## Arguments
    // /// 
    // /// `hdl` - Task handle to set argument for
    // /// `argid` - The index of the argument
    // /// `array_slice` - The data to pass
    // /// `flags` - Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
    // pub fn task_set_arg_mut<T>(hdl: *mut mcl_handle, argid: u64, array_slice: &mut [T], flags: u64) {
    //     low_level::task_set_arg_mut(hdl, argid, array_slice, flags);
    // }
    
    // pub fn task_set_arg<T>(hdl: *mut mcl_handle, argid: u64, array_slice: &[T], flags: u64) {
    //     low_level::task_set_arg(hdl, argid, array_slice, flags);

    // }


    // pub fn task_set_local(hdl: *mut mcl_handle, argid: u64, mem_size: usize, flags: u64) {
    //     low_level::task_set_local(hdl, argid, mem_size, flags);
    // }

    // /// Test whether a task has completed
    // /// 
    // /// ## Arguments
    // /// 
    // /// hdl - Reference to the task handle to test
    // /// 
    // /// Returns the status of the handle. One of the MCL_REQ_* constants
    // pub fn test(hdl: *mut mcl_handle) -> crate::ReqStatus {
    //     low_level::test(hdl)
    // }

    // /// Execute a specified task
    // /// 
    // /// ## Arguments
    // /// 
    // /// `hdl` - The handle to execute
    // /// `pes` - An array of size MCL_DEV_DIMS containing the number of threads in each dimension
    // /// `les` - An array of size MCL_DEV_DIMS contianing the local work dimensions
    // /// `type` - Specify compute locations using DevType::* enum 
    // pub fn exec(hdl: *mut mcl_handle, pes: &mut [u64; 3], les: &mut [u64; 3], t_type: crate::DevType) {
    //     low_level::exec(hdl, pes, les, t_type)
    // }

    // /// Wait for a task to complete
    // /// 
    // /// ## Arguments
    // /// 
    // /// `hdl` - The handle to wait for
    // pub fn wait(hdl: *mut mcl_handle) {
    //     low_level::wait(hdl);
    // }

    // /// Create a new MCL task
    // /// 
    // /// Returns a new task handle
    // pub fn task_create() -> *mut mcl_handle {
    //     low_level::task_create()
    // }

    // /// Frees data associated with the task handle
    // /// 
    // /// ## Arguments
    // /// 
    // /// * `hdl` - The handle to remove data for
    // pub fn task_free(hdl: *mut mcl_handle) {
    //     low_level::task_free(hdl);
    // }

    // /// Create and initialize a new MCL task
    // ///
    // /// ## Arguments
    // /// 
    // /// `path` - Path to the OpenCL file that include the kernel to run
    // /// `name` - The OpenCL kernel to run
    // /// `num_args` - The number of arguments the kernel accepts
    // /// `compile_args` - Compilation args to compile the kernel with
    // /// `flags` - 0 or MCL_FLAG_NO_RES
    // fn _task_init(path: &str, name: &str, num_args: u64, compile_args: &str, flags: u64) -> *mut mcl_handle {
    //     low_level::_task_init(path, name, num_args, compile_args, flags)
    // }

    // /// Create a new transfer request
    // /// 
    // /// ## Arguments
    // /// 
    // /// * `nargs` - Number of arguments to transfer
    // /// * `ncopies` - How many copies to create
    // /// * `flags` - Flags to pass to the transfer creation function
    // /// 
    // /// Returns a new transfer handle
    // #[cfg(not(feature="versal"))]
    // pub fn transfer_create(nargs: u64, ncopies: u64, _flags: u64) -> *mut mcl_transfer {
    //     low_level::transfer_create(nargs, ncopies, _flags)
    // }

    // /// Set up an argument associated with a transfer handle
    // /// 
    // /// # Arguments
    // /// 
    // /// * `t_hdl` - Transfer handle to set argument for
    // /// * `idx` - The index of the argument
    // /// * `array_slice` - The data to pass
    // /// * `flags` - Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
    // #[cfg(not(feature="versal"))]
    // pub fn transfer_set_arg_mut<T>(t_hdl: *mut mcl_transfer , idx: u64, array_slice: &mut [T], flags: u64) {
    //     low_level::transfer_set_arg_mut(t_hdl, idx, array_slice, flags)
    // }

    // #[cfg(not(feature="versal"))]
    // pub fn transfer_set_arg<T>(t_hdl: *mut mcl_transfer, idx: u64, array_slice: & [T], flags: u64) {
    //     low_level::transfer_set_arg(t_hdl, idx, array_slice, flags);
    // }

    // #[cfg(not(feature="versal"))]
    // pub fn transfer_set_local(t_hdl: *mut mcl_transfer, idx: u64, size: usize, flags: u64) {
    //     low_level::transfer_set_local(t_hdl, idx, size, flags)
    // }


    // /// Executes a transfer. Asychronously moves data
    // /// 
    // /// ## Arguments
    // /// 
    // /// * `t_hdl` - Transfer handle to execute
    // /// * `flags` - Flags to specify devices, same as exec(...)
    // #[cfg(not(feature="versal"))]
    // pub fn transfer_exec(t_hdl: *mut mcl_transfer, d_type: crate::DevType) {
    //     low_level::transfer_exec(t_hdl, d_type);
    // }

    // /// Wait for a transfer to complete
    // /// 
    // /// ## Arguments
    // /// 
    // /// `t_hdl` - The tranfser handle to wait for
    // #[cfg(not(feature="versal"))]
    // pub fn transfer_wait(t_hdl: *mut mcl_transfer) {
    //     low_level::transfer_wait(t_hdl);
    // }

    // /// Test whether a transfer has completed
    // /// 
    // /// ## Arguments
    // /// 
    // /// * t_hdl - The transfer handle to test
    // /// 
    // /// Returns the status of the handle. One of the MCL_REQ_* constants
    // #[cfg(not(feature="versal"))]
    // pub fn transfer_test(t_hdl: *mut mcl_transfer) -> i32 {
    //     low_level::transfer_test(t_hdl)
    // }

    // /// Frees data associated with the transfer handle
    // /// 
    // /// ## Arguments
    // /// 
    // /// * `t_hdl` - The handle to remove data for
    // #[cfg(not(feature="versal"))]
    // pub fn transfer_free(t_hdl: *mut mcl_transfer) {
    //     low_level::transfer_free(t_hdl);
    // }

    /// Get the info of a specific device
    /// 
    /// ## Arguments
    /// 
    /// * `id` - The ID of the device to retrieve info for
    /// 
    /// Returns the info of specificed device
    pub fn get_dev(&self, id: u32) -> crate::DevInfo {
        low_level::get_dev(id)
    }

    /// Get the number of devices in the system
    /// 
    /// Returns the number of devices available
    pub fn get_ndev(&self) -> u32 {
        low_level::get_ndev()
    }

    // /// Complete the task without executing  (i.e. trigger dependencies)
    // /// 
    // /// ## Arguments
    // /// 
    // /// * `hdl` - The task handle to complete
    // pub fn null(hdl: *mut mcl_handle) {
    //     low_level::null(hdl);
    // }

    // /// Initializes MCL
    // /// 
    // /// ## Arguments
    // /// 
    // /// * `workers` - The number of workers to use
    // /// * `flags` - Flags to pass to MCL
    // pub fn init(workers: u64, flags: u64) {
    //     low_level::init(workers, flags);
    // }
    
    // /// Finalizes MCL
    // pub fn finit() {
    //     low_level::finit();
    // }
}





#[cfg(feature="versal")]
pub struct TaskBinProps {
    
    c_handle:  mcl_bin_properties,
}

#[cfg(feature="versal")]
impl TaskBinProps {

    pub fn new(num_devices: u64, types: u64, name: &str) -> Self {

        TaskBinProps {
            c_handle : mcl_bin_properties {
                devices: num_devices,
                types: types,
                name: CString::new(name).unwrap().into_raw(),
            }
        }
    }

    pub fn get_devices(&self) -> u64 {
        self.c_handle.devices
    }

    pub fn get_types(&self) -> u64 {
        self.c_handle.types
    }

    pub fn get_name(&self) -> String {
        return unsafe{CString::from_raw(self.c_handle.name as *mut _).into_string().unwrap()};
    }
}

unsafe impl Send for TaskHandle {}
unsafe impl Sync for TaskHandle {}

/// An opaque struct that represents an MCL task that has been submitted for execution
pub struct TaskHandle {
    /// Pointer to the C task handle
    c_handle: *mut mcl_handle,
 }

impl TaskHandle {

    /// Wait for the task associated with the handle to complete
    /// 
    /// # Example
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    /// 
    ///     let hdl = Task::from("my_path", "my_kernel", 0)
    ///                 .flags(KernelFlag::SRC)
    ///                 .compile_with_args("-D MYDEF")
    ///                 .exec(pes);
    /// 
    ///     hdl.wait();
    ///  
    pub fn wait(&self) {
        low_level::wait(self.c_handle);
    }

    /// Check the status of the task associated with the handle
    /// 
    /// # Example
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    /// 
    ///     let hdl = Task::from("my_path", "my_kernel", 0)
    ///                 .flags(KernelFlag::SRC)
    ///                 .compile_with_args("-D MYDEF")
    ///                 .exec(pes);
    /// 
    ///     let hdl_status = hdl.test();
    ///   
    pub fn test(&self) -> ReqStatus {
        return low_level::test(self.c_handle);
    }

    /// Wait for the task associated with the handle to complete but release the task
    /// if not.
    /// 
    /// # Example
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    /// 
    ///     let hdl = Task::from("my_path", "my_kernel", 0)
    ///                 .flags(KernelFlag::SRC)
    ///                 .compile_with_args("-D MYDEF")
    ///                 .exec();
    /// 
    ///     let hdl_status = hdl.into_future();
    ///   
    pub async fn into_future(&self) {

        while self.test() != ReqStatus::COMPLETED {
            async_std::task::yield_now().await;
        } 
    }
}

impl Drop for TaskHandle {

    fn drop(&mut self) {
        low_level::task_free(self.c_handle);
    }
}

#[cfg(not(feature="versal"))]
/// An opaque struct that represents a submitted MCL tranfser request
pub struct TransferHandle {

   /// Pointer to the C transfer handle
    c_handle: *mut mcl_transfer,
 }

#[cfg(not(feature="versal"))]
impl TransferHandle {

    /// Wait for the transfer associated with the handle to complete
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///
    ///     let t_hdl = Transfer::new(1, 1, 0)
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .dev(DevType::CPU)
    ///                 .exec();
    ///     t_hdl.wait();
    pub fn wait(&self) {
        low_level::transfer_wait(self.c_handle);
    }

    /// Check the status of the transfer associated with the handle
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///
    ///     let t_hdl = Transfer::new(1, 1, 0)
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .dev(DevType::CPU)
    ///                 .exec();
    /// 
    ///     let t_hdl_status = t_hdl.test(); 
    pub fn test(&self) {
        low_level::transfer_test(self.c_handle);
    }
}

#[cfg(not(feature="versal"))]
impl Drop for TransferHandle {
    
    fn drop(&mut self) {
        low_level::transfer_free(self.c_handle);
    }
}

/// This structure is used to setup the MCL environment with the given parameters
pub struct MclEnvBuilder {
    num_workers: usize,
    flags: u64,
}


impl MclEnvBuilder {

    /// Creates and returns a new MclEnvBuilder with the default values
    /// 
    /// # Example
    /// 
    ///     let env = MclEnvBuilder::new()
    ///                 .initialize();
    /// 
    pub fn new() -> MclEnvBuilder {

        MclEnvBuilder {
            num_workers: 1,
            flags: 0,
        }
    }

    /// Set the flags to pass to the mcl initialization function
    /// 
    /// ## Arguments
    /// 
    /// * `flags` - The flags to pass to MCL
    /// 
    /// Returns the MclEnvBuilder with the flags set
    pub fn flags(mut self, flags: u64) -> MclEnvBuilder {
        
        self.flags =  flags;

        self
    }

    /// Set the num_workers to pass to the mcl initialization function
    /// 
    /// ## Arguments
    /// 
    /// * `num_workers` - The num_workers to pass to MCL
    /// 
    /// Returns the MclEnvBuilder with the num_workers set
    /// 
    /// # Example
    /// 
    ///     let env = MclEnvBuilder::new()
    ///                 .num_workers(1)
    ///                 .initialize();
    /// 
    pub fn num_workers(mut self, workers: usize) -> MclEnvBuilder {

        assert!(workers > 0);

        self.num_workers = workers;

        self
    }

    /// Initializes mcl
    /// 
    /// Returns an instance of MclEnv
    ///  
    /// # Example
    /// 
    ///     let env = MclEnvBuilder::new()
    ///                 .initialize();
    /// 
    pub fn initialize(self) -> Mcl {

        low_level::init(self.num_workers as u64, self.flags);

        Mcl{_env: MclEnv}
    }
}

/// Represents an initialize MCL environment. When this struct goes out of scope the MCL environment is finalized.
/// Thus, there is no need to explicitly call the equivalent of mcl_finit()
pub struct MclEnv;

impl MclEnv {

    /// Returns the number of devices available
    /// 
    /// # Example
    /// 
    ///     let env = MclEnvBuilder::new()
    ///                 .initialize();
    ///     
    ///     let num_devices = env.get_ndev();
    ///     println!("{} devices found", num_devices)
    /// 
    pub fn get_ndev(&self) -> usize {
        
        return  low_level::get_ndev() as usize;
    }
    
    /// Get the info of a specific device
    /// 
    /// ## Arguments
    /// 
    /// * `id` - The ID of the device to retrieve info for
    /// 
    /// Returns the info of specificed device
    /// 
    /// # Example
    /// 
    ///     let env = MclEnvBuilder::new()
    ///                 .initialize();
    ///     
    ///     let num_devices = env.get_ndev();
    ///     for i in 0..num_devices {
    ///         let dev = env.get_ndev(i)
    ///         println!("{} device found", dev.name)
    ///     }
    /// 
    pub fn get_dev(&self, id: usize) -> DevInfo {
    
        low_level::get_dev(id as u32)
    }
}


impl Drop for MclEnv {

    /// Finalizes mcl when MclEnv goes out of scope
    fn drop(&mut self) {
        low_level::finit();
    }
}


/// Represents an incomplete MCL task whose kernel has been set but the arguments and target device are missing 
pub struct CompiledTask {
    curr_arg: usize,
    les: [u64; 3],
    dev: DevType,
    hdl: TaskHandle,
}

impl CompiledTask {
    
    #[cfg(feature="versal")]
    pub fn add_binary(self, prog_path: &str, props: TaskBinProps) -> Self {
        low_level::task_set_binary(self.hdl.c_handle, props.c_handle, prog_path);

        self
    }

    /// Set a new argument to the mcl task in preparation
    /// 
    /// ## Arguments
    /// 
    /// * `arg` - The TaskArg that is needed by the task kernel
    /// 
    /// Returns the CompiledTask with the arg set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    /// 
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .flags(KernelFlag::SRC)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    ///   
    pub fn arg<T>(mut self, arg: TaskArg<T>) -> Self {
            
        match arg.data {
            TaskArgData::Mutable(mut x) => low_level::task_set_arg_mut(self.hdl.c_handle, self.curr_arg as u64, &mut x, arg.flags),
            TaskArgData::Immutable(x) => low_level::task_set_arg(self.hdl.c_handle, self.curr_arg as u64, x, arg.flags),
            TaskArgData::NoData(x) => low_level::task_set_local(self.hdl.c_handle, self.curr_arg as u64, x, arg.flags)
        }
        self.curr_arg += 1;

        self
    }

    /// Set the local workgroup size to the mcl task in preparation
    /// 
    /// ## Arguments
    /// 
    /// * `les` - The local workgroupsize that is needed by the task kernel
    /// 
    /// Returns the CompiledTask with the local workgroup size  set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .flags(KernelFlag::SRC)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    pub fn lwsize(mut self, les: [u64; 3]) -> Self {
       
        self.les = les;

        return self
    }

    /// Set the preferred device to the mcl task in preparation
    /// 
    /// ## Arguments
    /// 
    /// * `dev` - The device to execute the task kernel
    /// 
    /// Returns the CompiledTask with the desired device  set
    ///
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .flags(KernelFlag::SRC)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    pub fn dev(mut self, dev: DevType) -> Self {

        self.dev = dev;

        return self;
    }

    /// Submit the task for execution
    /// 
    /// ## Arguments
    /// 
    /// * `pes` - The global workgroup size of the  task kernel
    /// 
    /// Returns a new task handle that can be queried for completion
    ///     
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    pub fn exec(mut self, ref mut pes: [u64; 3]) -> TaskHandle {

        low_level::exec(self.hdl.c_handle, pes, &mut self.les, self.dev);

        return self.hdl;
    }

    /// Set the task as completed
    /// 
    /// Returns a new task handle that can be queried for completion
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .null();
    /// 
    pub fn null(self) -> TaskHandle {

        low_level::null(self.hdl.c_handle);

        return self.hdl;
    }

}

pub enum TaskArgData<'a,T> {
    Mutable(&'a mut [T]),
    Immutable(&'a [T]),
    NoData(usize),
}

/// Represents a data argument for an MCL task along with the use flags (e.g. input, output, access type etc.)
pub struct TaskArg<'a,T> {

    data: TaskArgData<'a,T>,
    flags: u64,
}

impl<'a, T> TaskArg<'a, T> {


    /// Create a new task input argument from a slice 
    /// 
    /// ## Arguments
    /// 
    /// * `slice` - The slice used as input argument
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    pub fn input_slice(slice: &'a [T]) -> Self {
        
        TaskArg {
            data: TaskArgData::Immutable(slice),
            flags: ArgOpt::INPUT | ArgType::BUFFER,
        }
    }

    // [TODO] How to avoid matching slices??
    /// Create a new task input argument from a scalar 
    /// 
    /// ## Arguments
    /// 
    /// * `scalar` - The scalar used as input argument
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Example
    ///     
    ///     let data = 4;
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_scalar(&data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///  
    pub fn input_scalar(scalar: &'a T) -> Self {
        
        TaskArg {
            data: TaskArgData::Immutable(std::slice::from_ref(scalar)),
            flags: ArgOpt::INPUT | ArgType::SCALAR,
        }
    }
    /// Requests the allocation of local memory
    /// 
    /// ## Arguments
    /// 
    /// * `mem_size` - The amount of local memory requested
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Example
    ///     
    ///     let data = 4;
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_local(400))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///  
    pub fn input_local(mem_size: usize) -> Self {

        TaskArg {
            data: TaskArgData::NoData(mem_size),
            flags: ArgOpt::LOCAL,
        }
    }


    /// Create a new task output argument from a slice 
    /// 
    /// ## Arguments
    /// 
    /// * `slice` - The slice used as output argument
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Example
    ///     
    ///     let mut data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::output_slice(&mut data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///   
    pub fn output_slice(slice: &'a mut [T]) -> Self {
        
        TaskArg {
            data: TaskArgData::Mutable( slice),
            flags: ArgOpt::OUTPUT | ArgType::BUFFER,
        }
    }

    /// Create a new task output argument from a scalar 
    /// 
    /// ## Arguments
    /// 
    /// * `scalar` - The scalar used as output argument
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Example
    ///     
    ///     let mut data = 4;
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::output_scalar(&mut data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///   
    pub fn output_scalar(scalar: &'a mut T) -> Self {
        
        TaskArg {
            data: TaskArgData::Immutable(std::slice::from_mut(scalar)),
            flags: ArgOpt::OUTPUT | ArgType::SCALAR,
        }
    }

    /// Create a new task input+output argument from a slice 
    /// 
    /// ## Arguments
    /// 
    /// * `slice` - The slice used as input+output argument
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Example
    ///     
    ///     let mut data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::inout_slice(&mut data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///    
    pub fn inout_slice(slice: &'a mut [T]) -> Self {
        TaskArg {
            data: TaskArgData::Mutable(slice),
            flags: ArgOpt::OUTPUT | ArgOpt::INPUT | ArgType::BUFFER,
        }
    }

    /// Create a new task input+output argument from a scalar 
    /// 
    /// ## Arguments
    /// 
    /// * `scalar` - The scalar used as input+output argument
    /// 
    /// Returns a new TaskArg
    ///     
    /// # Example
    ///     
    ///     let mut data = 4;
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::inout_scalar(&mut data))
    ///                 .lwsize(les)
    ///                 .dev(DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///   
    pub fn inout_scalar(scalar: &'a mut T) -> Self {
       
        TaskArg {
            data: TaskArgData::Mutable(std::slice::from_mut(scalar)),
            flags: ArgOpt::OUTPUT | ArgOpt::INPUT | ArgType::SCALAR,
        }
    }

    /// Sets the resident memory flag for the argument
    /// 
    /// ## Arguments
    /// 
    /// * `use_res` - Whether to set the resident memory flag or not
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .resident(true)
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    pub fn resident(mut self, use_res: bool) -> Self {

        if use_res {
            self.flags = self.flags | ArgOpt::RESIDENT;
        }

        return self;
    }

    /// Sets the dynamic memory flag for the argument
    /// 
    /// ## Arguments
    /// 
    /// * `use_dyn` - Whether to set the dynamic memory flag or not
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .dynamic(true)
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    pub fn dynamic(mut self, use_dyn: bool) -> Self {
        
        if use_dyn {
            self.flags = self.flags | ArgOpt::DYNAMIC;
        }

        return self;
    }

    /// Sets the done flag for the argument
    /// 
    /// ## Arguments
    /// 
    /// * `use_done` - Whether to set the done flag or not
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .done(true)
    ///                 .exec(pes)
    ///                 .wait();
    ///  
    pub fn done(mut self, use_done: bool) -> Self {
        
        if use_done {
            self.flags = self.flags | ArgOpt::DONE;
        }
   
        return self;
    }

    /// Sets the invalid flag for the argument
    /// 
    /// ## Arguments
    /// 
    /// * `use_inv` - Whether to set the invalid flag or not
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .invalid(true)
    ///                 .exec(pes)
    ///                 .wait();
    ///  
    pub fn invalid(mut self, use_inv: bool) -> Self {
        
        if use_inv {
            self.flags = self.flags | ArgOpt::INVALID;
        }
    
        return self;
    }

    /// Sets the read_only flag for the argument
    /// 
    /// ## Arguments
    /// 
    /// * `use_ronly` - Whether to set the read_only flag or not
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .read_only(true)
    ///                 .exec(pes)
    ///                 .wait();
    ///  
    pub fn read_only(mut self, use_ronly: bool) -> Self {

        if use_ronly {
            self.flags = self.flags | ArgOpt::RDONLY;
        }
   
        return self;
    }

    /// Sets the write_only flag for the argument
    /// 
    /// ## Arguments
    /// 
    /// * `use_wronly` - Whether to set the write_only flag or not
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = vec![1, 1, 1];
    ///     let pes: [u64; 3] = vec![1, 1, 1];
    ///     let hdl = Task::from("my_path", "my_kernel", 1)
    ///                 .compile()
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .write_only(true)
    ///                 .exec(pes)
    ///                 .wait();
    ///  
    pub fn write_only(mut self, use_wronly: bool) -> Self {

        if use_wronly {
            self.flags = self.flags | ArgOpt::WRONLY;
        }
    
        return self;
    }

    /// Overwrites the flags already set for the argument with the given ones
    /// 
    /// ## Arguments
    /// 
    /// * `bit_flags` - The bit_flags to replace existing flags with
    /// 
    /// Returns the TaskArg with the preference set
    pub fn bit_flags(mut self, bit_flags: u64) -> Self {

        self.flags = bit_flags;

        return self;
    }
} 


/// A Task can is used to create an incomplete mcl task from source or binary
pub struct Task {

    prog_path: String,
    kernel_name: String,
    compile_args: String,
    kernel_flag: KernelFlag,
    num_args: usize,
    hdl: TaskHandle,
}

impl Task {


    /// Creates a new mcl task from the given kernel
    /// 
    /// ## Arguments
    /// 
    /// * `prog_path` - The path to the file where the kernel resides
    /// * `kernel_name` - The kernel name
    /// * `num_args` - The number of arguments of the kernel
    /// 
    /// Returns a new Task that can be compiled to a CompiledTask
    /// 
    /// # Example
    /// 
    ///     let t = Task::from("my_path", "my_kernel", 2);
    /// 
    pub fn from(prog_path: &str, kernel_name: &str, num_args: usize) -> Self {

        Task {
            prog_path: prog_path.to_string(),
            kernel_name: kernel_name.to_string(), 
            compile_args: "".to_string(),
            #[cfg(feature="versal")]
            kernel_flag: KernelFlag::SRC,
            #[cfg(not(feature="versal"))]
            kernel_flag: KernelFlag::NONE,
            num_args: num_args,
            hdl: TaskHandle { c_handle: low_level::task_create(), },
        }
    }


    /// Sets extra flags for the CompiledTask
    /// 
    /// ## Arguments
    /// 
    /// * `flags` - The extra flags to use for the compilation
    /// 
    /// Returns the Compiled task with the flags set
    /// 
    /// # Example
    /// 
    ///     let compiled_t = Task::from("my_path", "my_kernel", 0)
    ///                 .flags(KernelFlag::SRC)
    ///                 .compile_with_args("-D MYDEF");
    /// 
    pub fn flags(mut self, k_flag: KernelFlag) -> Self {

        self.kernel_flag = k_flag;

        self
    }

    /// Creates a CompiledTask from the Task
    /// 
    /// Returns a new CompiledTask
    /// 
    /// # Example
    /// 
    ///     let compiled_t = Task::from("my_path", "my_kernel", 2)
    ///                 .compile();
    /// 
    pub fn compile(self) -> CompiledTask {

        low_level::task_set_kernel(self.hdl.c_handle, &self.prog_path, &self.kernel_name, self.num_args as u64, &self.compile_args, self.kernel_flag);

        CompiledTask {
            // num_args: self.num_args,
            curr_arg: 0,
            hdl: self.hdl,
            dev: DevType::GPU,
            les: [1; 3],
        }
    }

    /// Creates a CompiledTask from the Task with the given compilation args
    /// 
    /// ## Arguments
    /// 
    /// * `compile_args` - The compile_args to use for the compilation
    /// 
    /// Returns a new CompiledTask
    /// 
    ///     
    /// # Example
    /// 
    ///     let compiled_t = Task::from("my_path", "my_kernel", 2)
    ///                 .compile_with_args("-D MYDEF");
    /// 
    pub fn compile_with_args(mut self, compile_args: &str) -> CompiledTask {

        self.compile_args = compile_args.to_string();
        low_level::task_set_kernel(self.hdl.c_handle, &self.prog_path, &self.kernel_name, self.num_args as u64, &self.compile_args, self.kernel_flag);
    
        CompiledTask {
            // num_args: self.num_args,
            curr_arg: 0,
            hdl: self.hdl,
            dev: DevType::GPU,
            les: [1; 3],
        }
    }
}



/// Wait for all pending tasks to complete
///     
/// # Example
///     
///     let data = vec![0; 4];
///     let les: [u64; 3] = vec![1, 1, 1];
///     let pes: [u64; 3] = vec![1, 1, 1];
///     let hdl = Task::from("my_path", "my_kernel", 1)
///                 .compile()
///                 .arg(TaskArg::input_slice(&data))
///                 .write_only(true)
///                 .exec(pes);
///      wait_all();
///
pub fn wait_all() {

    let err = unsafe { mcl_wait_all() };
    if err != 0 {
        panic!("Error {}. Wait all failed.", err);
    }
}


#[cfg(not(feature="versal"))]
/// Transfer can be used to create a request for data transfer from MCL.
pub struct Transfer {

    // num_args: usize,
    curr_arg: usize,
    d_type: DevType,
    hdl: TransferHandle,
}

#[cfg(not(feature="versal"))]
impl Transfer {
    
    /// Creates a new transfer with the given parameters
    /// 
    /// ## Arguments
    /// * `num_args` - The number of arguments that will be transfered
    /// * `ncopies` - The number of copies to create
    /// * `flags` - Other related flags
    /// 
    /// Returns a new  Transfer object
    /// 
    /// # Example
    ///     let tr = Transfer::new(1, 1, 0);
    ///
    pub fn new(num_args: usize, ncopies: usize, flags: u64) -> Self {
        Transfer {
            //num_args: num_args,
            curr_arg: 0,
            d_type: DevType::GPU,
            hdl: TransferHandle{ c_handle: low_level::transfer_create(num_args as u64, ncopies as u64, flags), },
        }
    }


    /// Adds an argument to be transferred by this request
    /// 
    /// ## Arguments
    /// * ` arg` - The argument to be transferred enclosed in a TaskArg
    /// 
    /// Returns the Transfer object
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///
    ///     let tr = Transfer::new(1, 1, 0)
    ///                 .arg(TaskArg::input_slice(&data));
    ///
    pub fn arg<T>(mut self, arg: TaskArg<T>) -> Self {
            
        match arg.data {
            TaskArgData::Mutable(mut x) => low_level::transfer_set_arg_mut(self.hdl.c_handle, self.curr_arg as u64, &mut x, arg.flags),
            TaskArgData::Immutable(x) => low_level::transfer_set_arg(self.hdl.c_handle, self.curr_arg as u64, x, arg.flags),
            TaskArgData::NoData(x) => low_level::transfer_set_local(self.hdl.c_handle, self.curr_arg as u64, x, arg.flags)
        }
        self.curr_arg += 1;

        self
    }


    /// Sets the desired device type
    /// 
    /// ## Arguments
    /// 
    /// * `d_type` - The device type to transfer to
    /// 
    /// Returns the Transfer with the preference set
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///
    ///     let tr = Transfer::new(1, 1, 0)
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .dev(DevType::CPU);
    /// 
    pub fn dev(mut self, d_type: DevType) -> Self {
        
        self.d_type = d_type;
        self
    }

    /// Submit the transfer request
    /// 
    /// Returns a TransferHandle that can be queried for completion
    /// 
    /// # Example
    ///     
    ///     let data = vec![0; 4];
    ///
    ///     let t_hdl = Transfer::new(1, 1, 0)
    ///                 .arg(TaskArg::input_slice(&data))
    ///                 .dev(DevType::CPU)
    ///                 .exec();
    ///     t_hdl.wait();
    pub fn exec(self) -> TransferHandle {

        low_level::transfer_exec(self.hdl.c_handle, self.d_type);

        self.hdl
    }

}

mod low_level {

    use std::slice;
    use std::ptr::null_mut;
    use libmcl_sys::*;
    use std::ffi::{c_void, CString, CStr};
    use std::mem::size_of;



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
    pub(crate) fn task_set_kernel(hdl: *mut mcl_handle, path: &str, name: &str, num_args: u64, compile_args: &str, flags: crate::KernelFlag ) {
        let flag = match flags {
            crate::KernelFlag::NONE => 0,
            #[cfg(feature="versal")]
            crate::KernelFlag::SRC => MCL_KERNEL_SRC,
            #[cfg(feature="versal")]
            crate::KernelFlag::BIN => MCL_KERNEL_BIN,
            #[cfg(feature="versal")]
            crate::KernelFlag::SIMDEV => MCL_KERNEL_SIMDEV,
            crate::KernelFlag::NORES => MCL_FLAG_NO_RES,
        };
        unsafe {
            mcl_task_set_kernel(hdl, CString::new(path).unwrap().into_raw(), CString::new(name).unwrap().into_raw(), num_args,  CString::new(compile_args).unwrap().into_raw(), flag.into());
        }
    }


    /// Initialize a task to run the specified binarykernel
    /// 
    /// ## Arguments
    /// 
    /// `hdl` - Handle associated with task
    /// `path` - Path to the binary file that implements the kernel to run
    /// `num_args` - The number of arguments the kernel accepts
    #[cfg(feature="versal")]
    pub(crate) fn task_set_binary(hdl: *mut mcl_handle, props: mcl_bin_properties, path: &str) {
        
        unsafe {
            mcl_task_set_binary(hdl, props, CString::new(path).unwrap().into_raw());
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
    pub(crate) fn task_set_arg_mut<T>(hdl: *mut mcl_handle, argid: u64, array_slice: &mut [T], flags: u64) {
        
        let err = unsafe { mcl_task_set_arg(hdl, argid, array_slice.as_mut_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, flags) };
        if err != 0 {
            panic!("Error {}. Could not set argument for TaskHandle", err);
        }
    }
    
    pub(crate) fn task_set_arg<T>(hdl: *mut mcl_handle, argid: u64, array_slice: &[T], flags: u64) {

        let err = unsafe { mcl_task_set_arg(hdl, argid, array_slice.as_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, flags) };
        if err != 0 {
            panic!("Error {}. Could not set argument for TaskHandle", err);
        }

    }


    pub(crate) fn task_set_local(hdl: *mut mcl_handle, argid: u64, mem_size: usize, flags: u64) {

        let err = unsafe { mcl_task_set_arg(hdl, argid, null_mut(), mem_size as u64, flags) };
        if err != 0 {
            panic!("Error {}. Could not set argument for TaskHandle", err);
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
            MCL_REQ_COMPLETED  => crate::ReqStatus::COMPLETED,    
            MCL_REQ_ALLOCATED  => crate::ReqStatus::ALLOCATED,     
            MCL_REQ_PENDING    => crate::ReqStatus::PENDING,     
            MCL_REQ_INPROGRESS => crate::ReqStatus::INPROGRESS,     
            MCL_REQ_FINISHING  => crate::ReqStatus::FINISHING, 
            _                  => crate::ReqStatus::UNKNOWN,     
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
            #[cfg(feature="versal")]
            crate::DevType::NVDLA => MCL_TASK_NVDLA,
            #[cfg(feature="versal")]
            crate::DevType::SIMDEV => MCL_TASK_SIMDEV,
            crate::DevType::ANY => MCL_TASK_ANY,
        };
        let err = unsafe { mcl_exec(hdl, pes.as_mut_ptr() as *mut _ as *mut u64, les.as_mut_ptr() as *mut _ as *mut u64, flags.into()) };
        if err != 0 {
            panic!("Error {}. Could not execute task.", err);
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

    /// Create a new transfer request
    /// 
    /// ## Arguments
    /// 
    /// * `nargs` - Number of arguments to transfer
    /// * `ncopies` - How many copies to create
    /// * `flags` - Flags to pass to the transfer creation function
    /// 
    /// Returns a new transfer handle
    #[cfg(not(feature="versal"))]
    pub(crate) fn transfer_create(nargs: u64, ncopies: u64, _flags: u64) -> *mut mcl_transfer {

        let transfer_hdl = unsafe { mcl_transfer_create(nargs, ncopies) };
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
    #[cfg(not(feature="versal"))]
    pub(crate) fn transfer_set_arg_mut<T>(t_hdl: *mut mcl_transfer , idx: u64, array_slice: &mut [T], flags: u64) {

        let err = unsafe { mcl_transfer_set_arg(t_hdl, idx, array_slice.as_mut_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, flags) };
        if err != 0 {
            panic!("Error {}. Could not set transfer argument", err);
        }
    }

    #[cfg(not(feature="versal"))]
    pub(crate) fn transfer_set_arg<T>(t_hdl: *mut mcl_transfer, idx: u64, array_slice: & [T], flags: u64) {

        let err = unsafe { mcl_transfer_set_arg(t_hdl, idx, array_slice.as_ptr() as *mut c_void, (array_slice.len() * size_of::<T>()) as u64, flags) };
        if err != 0 {
            panic!("Error {}. Could not set transfer argument", err);
        }
    }

    #[cfg(not(feature="versal"))]
    pub(crate) fn transfer_set_local(t_hdl: *mut mcl_transfer, idx: u64, size: usize, flags: u64) {

        let err = unsafe { mcl_transfer_set_arg(t_hdl, idx, null_mut(), size as u64, flags) };
        if err != 0 {
            panic!("Error {}. Could not set transfer argument", err);
        }
    }


    /// Executes a transfer. Asychronously moves data
    /// 
    /// ## Arguments
    /// 
    /// * `t_hdl` - Transfer handle to execute
    /// * `flags` - Flags to specify devices, same as exec(...)
    #[cfg(not(feature="versal"))]
    pub(crate) fn transfer_exec(t_hdl: *mut mcl_transfer, d_type: crate::DevType) {

        let flags =  match d_type {
            crate::DevType::NONE => MCL_TASK_NONE,
            crate::DevType::CPU => MCL_TASK_CPU,
            crate::DevType::GPU => MCL_TASK_GPU,
            crate::DevType::FPGA => MCL_TASK_FPGA,
            #[cfg(feature="versal")]
            crate::DevType::NVDLA => MCL_TASK_NVDLA,
            #[cfg(feature="versal")]
            crate::DevType::SIMDEV => MCL_TASK_SIMDEV,
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
    #[cfg(not(feature="versal"))]
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
    #[cfg(not(feature="versal"))]
    pub(crate) fn transfer_test(t_hdl: *mut mcl_transfer) -> i32 {
        
        let err = unsafe { mcl_transfer_test(t_hdl) };
        return err;
    }

    /// Frees data associated with the transfer handle
    /// 
    /// ## Arguments
    /// 
    /// * `t_hdl` - The handle to remove data for
    #[cfg(not(feature="versal"))]
    pub(crate) fn transfer_free(t_hdl: *mut mcl_transfer) {

        let err = unsafe { mcl_transfer_free(t_hdl) };
        if err != 0 {
            panic!("Error {}. Could not free transfer handle", err);
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
}

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
//     task_set_arg(&hdl, 0, &mut array[..], ArgType::BUFFER| ArgOpt::INPUT); 
//     task_set_arg(&hdl, 1, slice::from_mut(& mut len), ArgType::SCALAR| ArgOpt::INPUT); 
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
//     task_set_arg(&hdl, 0, &mut array[..], ArgType::BUFFER| ArgOpt::INPUT); 
//     task_set_arg(&hdl, 1, slice::from_mut(& mut len), ArgType::SCALAR| ArgOpt::INPUT); 
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
//             ReqStatus::COMPLETED => break,
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
//             ReqStatus::COMPLETED => true,
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