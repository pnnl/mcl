use libmcl_sys::*;
use crate::low_level;
use crate::device::DevType;

use bitflags::bitflags;


#[derive(Clone, PartialEq)]
pub enum ReqStatus {
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
    }
}

unsafe impl Send for TaskHandle {}
unsafe impl Sync for TaskHandle {}


// A Task can is used to create an incomplete mcl task from source or binary
pub(crate) struct InnerTask {

    // prog_path: String,
    kernel_name: String,
    num_args: usize,
    hdl: TaskHandle,
}

impl InnerTask {

    pub(crate) fn from( kernel_name: &str, num_args: usize) -> Self {

        InnerTask {
            kernel_name: kernel_name.to_string(), 
            num_args: num_args,
            hdl: TaskHandle { c_handle: low_level::task_create(), },
        }
    }

    pub(crate) fn compile(self) -> Task {

        low_level::task_set_kernel(self.hdl.c_handle,  &self.kernel_name, self.num_args as u64);

        Task {
            // num_args: self.num_args,
            curr_arg: 0,
            hdl: self.hdl,
            dev: DevType::ANY,
            les: [1; 3],
        }
    }
}



/// An opaque struct that represents an MCL task that has been submitted for execution
pub struct TaskHandle {
    /// Pointer to the C task handle
    c_handle: *mut mcl_handle,
 }

impl TaskHandle {

    /// Wait for the task associated with the handle to complete
    /// 
    /// # Examples
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let pes: [u64; 3] = [1, 1, 1];
    /// 
    ///     let hdl = mcl.task("my_kernel", 0)
    ///                 .exec(pes);
    /// 
    ///     hdl.wait();
    ///```  
    pub fn wait(&self) {
        low_level::wait(self.c_handle);
    }

    /// Check the status of the task associated with the handle
    /// 
    /// # Examples
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let pes: [u64; 3] = [1, 1, 1];
    /// 
    ///     let hdl = mcl.task("my_kernel", 0)
    ///                 .exec(pes);
    /// 
    ///     let hdl_status = hdl.test();
    ///``` 
    pub fn test(&self) -> ReqStatus {
        return low_level::test(self.c_handle);
    }

    /// Wait for the task associated with the handle to complete but release the task
    /// if not.
    /// 
    /// # Examples
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let pes: [u64; 3] = [1, 1, 1];
    /// 
    ///     let hdl = mcl.task("my_kernel", 0)
    ///                 .exec(pes);
    /// 
    ///     let hdl_status = hdl.into_future();
    ///``` 
    pub async fn into_future(&self) {

        while self.test() != ReqStatus::Completed {
            async_std::task::yield_now().await;
        } 
    }
}

impl Drop for TaskHandle {

    fn drop(&mut self) {
        low_level::task_free(self.c_handle);
    }
}


/// Represents an incomplete MCL task whose kernel has been set but the arguments and target device are missing 
pub struct Task {
    curr_arg: usize,
    les: [u64; 3],
    dev: DevType,
    hdl: TaskHandle,
}

impl Task {

    /// Set a new argument `arg` for `this` mcl task in preparation
    /// 
    /// Returns the Task with the arg set
    /// 
    /// # Examples
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     
    ///     let data = vec![0; 4];
    ///     let pes: [u64; 3] = [1, 1, 1];
    /// 
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    ///``` 
    pub fn arg<T>(mut self, arg: TaskArg<T>) -> Self {
            
        match arg.data {
            TaskArgData::Mutable(mut x) => low_level::task_set_arg_mut(self.hdl.c_handle, self.curr_arg as u64, &mut x, arg.flags),
            TaskArgData::Immutable(x) => low_level::task_set_arg(self.hdl.c_handle, self.curr_arg as u64, x, arg.flags),
            TaskArgData::NoData(x) => low_level::task_set_local(self.hdl.c_handle, self.curr_arg as u64, x, arg.flags)
        }
        self.curr_arg += 1;

        self
    }

    /// Set a new argument buffer `arg` for `this` mcl task in preparation
    /// 
    /// Returns the Task with the arg set
    /// 
    /// # Examples
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     
    ///     let data = vec![0; 4];
    ///     let pes: [u64; 3] = [1, 1, 1];
    /// 
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .exec(pes)
    ///                 .wait();
    /// 
    ///``` 
    pub fn arg_buffer<T>(mut self, arg: TaskArg<T>) -> Self { //TODO fix the offset issue
            
        match arg.data {
            TaskArgData::Mutable(mut x) => low_level::task_set_arg_buffer_mut(self.hdl.c_handle, self.curr_arg as u64, &mut x, 0, arg.flags),
            TaskArgData::Immutable(x) => low_level::task_set_arg_buffer(self.hdl.c_handle, self.curr_arg as u64, x, 0, arg.flags),
            TaskArgData::NoData(x) => panic!("invalid Task Argument Type"),
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
    /// Returns the Task with the local workgroup size  set
    /// 
    /// # Examples
    ///    
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load(); 
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
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
    /// Returns the Task with the desired device  set
    ///
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
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
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
    pub fn exec(mut self, ref mut pes: [u64; 3]) -> TaskHandle {

        low_level::exec(self.hdl.c_handle, pes, &mut self.les, self.dev);

        return self.hdl;
    }

    /// Set the task as completed
    /// 
    /// Returns a new task handle that can be queried for completion
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .null();
    ///``` 
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

    pub(crate) data: TaskArgData<'a,T>,
    pub(crate) flags: ArgOpt,
}

impl<'a, T> TaskArg<'a, T> {


    /// Create a new task input argument from `slice` 
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
    pub fn input_slice(slice: &'a [T]) -> Self {
        
        TaskArg {
            data: TaskArgData::Immutable(slice),
            flags: ArgOpt::INPUT | ArgOpt::BUFFER,
        }
    }

    // [TODO] How to avoid matching slices??
    /// Create a new task input argument from `scalar`
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = 4;
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_scalar(&data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///```  
    pub fn input_scalar(scalar: &'a T) -> Self {
        
        TaskArg {
            data: TaskArgData::Immutable(std::slice::from_ref(scalar)),
            flags: ArgOpt::INPUT|ArgOpt::SCALAR,
        }
    }
    /// Requests an allocation of `num_elems` of type `T`
    /// 
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = 4;
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::<u8>::input_local(400))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///```  
    pub fn input_local(num_elems: usize) -> Self {

        TaskArg{
            data: TaskArgData::NoData(num_elems*std::mem::size_of::<T>()),
            flags: ArgOpt::LOCAL,
        }
    }


    /// Create a new task output argument from `slice` 
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let mut data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::output_slice(&mut data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
    pub fn output_slice(slice: &'a mut [T]) -> Self {
        
        TaskArg {
            data: TaskArgData::Mutable( slice),
            flags: ArgOpt::OUTPUT |ArgOpt::BUFFER,
        }
    }

    /// Create a new task output argument from `scalar` 
    ///
    /// Returns a new TaskArg
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let mut data = 4;
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::output_scalar(&mut data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
    pub fn output_scalar(scalar: &'a mut T) -> Self {
        
        TaskArg {
            data: TaskArgData::Immutable(std::slice::from_mut(scalar)),
            flags: ArgOpt::OUTPUT |ArgOpt::SCALAR,
        }
    }

    /// Create a new task input+output argument from `slice` 
    /// 
    /// Returns a new TaskArg
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let mut data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::inout_slice(&mut data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
    pub fn inout_slice(slice: &'a mut [T]) -> Self {
        TaskArg {
            data: TaskArgData::Mutable(slice),
            flags: ArgOpt::OUTPUT | ArgOpt::INPUT|ArgOpt::BUFFER,
        }
    }

    /// Create a new task input+output argument from `scalar` 
    /// 
    /// Returns a new TaskArg
    ///     
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let mut data = 4;
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::inout_scalar(&mut data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
    pub fn inout_scalar(scalar: &'a mut T) -> Self {
       
        TaskArg {
            data: TaskArgData::Mutable(std::slice::from_mut(scalar)),
            flags: ArgOpt::OUTPUT | ArgOpt::INPUT|ArgOpt::SCALAR,
        }
    }

    /// Sets the resident memory flag for the argument
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).resident())
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
    pub fn resident(mut self) -> Self {
        self.flags = self.flags | ArgOpt::RESIDENT;
        return self;
    }

    /// Sets the dynamic memory flag for the argument
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).dynamic())
    ///                 .exec(pes)
    ///                 .wait();
    ///``` 
    pub fn dynamic(mut self) -> Self {
        self.flags = self.flags | ArgOpt::DYNAMIC;
        return self;
    }

    /// Sets the done flag for the argument
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).done())
    ///                 .exec(pes)
    ///                 .wait();
    ///```  
    pub fn done(mut self) -> Self {
        self.flags = self.flags | ArgOpt::DONE;
        return self;
    }

    /// Sets the invalid flag for the argument
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).invalid())
    ///                 .exec(pes)
    ///                 .wait();
    ///```  
    pub fn invalid(mut self) -> Self {
        self.flags = self.flags | ArgOpt::INVALID;
        return self;
    }

    /// Sets the read_only flag for the argument
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).read_only())
    ///                 .exec(pes)
    ///                 .wait();
    ///```  
    pub fn read_only(mut self) -> Self {
        self.flags = self.flags | ArgOpt::RDONLY;
        return self;
    }

    /// Sets the write_only flag for the argument
    /// 
    /// Returns the TaskArg with the preference set
    /// 
    /// # Examples
    ///     
    ///```no_run 
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let les: [u64; 3] = [1, 1, 1];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let hdl = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).write_only())
    ///                 .exec(pes)
    ///                 .wait();
    ///```
    pub fn write_only(mut self) -> Self {
        self.flags = self.flags | ArgOpt::WRONLY;
        return self;
    }

    // /// Overwrites the flags already set for the argument with the given ones
    // /// 
    // /// ## Arguments
    // /// 
    // /// * `bit_flags` - The bit_flags to replace existing flags with
    // /// 
    // /// Returns the TaskArg with the preference set
    // pub fn bit_flags(mut self, bit_flags: ArgOpt) -> Self {

    //     self.flags = bit_flags;

    //     return self;
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