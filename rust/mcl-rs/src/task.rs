use crate::device::DevType;
use crate::low_level;
use crate::low_level::{ArgOpt, ReqStatus, TaskOpt};
use crate::registered_buffer::RegisteredBuffer;
#[cfg(feature = "shared_mem")]
use crate::registered_buffer::SharedMemBuffer;
use libmcl_sys::*;

unsafe impl Send for Task<'_> {}
unsafe impl Sync for Task<'_> {}

/// Represents an MCL task whose kernel has been set but the arguments and target device are missing
pub struct Task<'a> {
    // we want to stare the actual reference to the original argument so we can track lifetimes appropriately
    // this also will allow us to protect agains the same argument being used as an output simulataneous
    // aka this prevents us from doing dirty C-memory things :)
    args: Vec<TaskArgOrBuf<'a>>,
    curr_arg: usize,
    les: Option<[u64; 3]>,
    dev: DevType,
    c_handle: *mut mcl_handle,
    shared_id: Option<u32>,
}

impl<'a> Task<'a> {
    pub(crate) fn new(kernel_name_cl: &str, nargs: usize, flags: TaskOpt) -> Self {
        let c_handle = low_level::task_create(flags);
        #[allow(unused_mut)]
        let mut id = None;
        #[cfg(feature = "shared_mem")]
        if flags.contains(TaskOpt::SHARED) {
            low_level::get_shared_task_id(c_handle)
        }
        let task = Task {
            args: vec![Default::default(); nargs],
            curr_arg: 0,
            les: None,
            dev: DevType::ANY,
            c_handle: c_handle,
            shared_id: id,
        };
        low_level::task_set_kernel(task.c_handle, kernel_name_cl, nargs as u64);
        task
    }

    /// Set a new argument `arg` for `this` mcl task.
    /// Note the order in which tasks are set must match the order expected by the kernel
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
    ///     let mut out = vec![0; 4];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data)) //the first argument to the kernel is  the input array
    ///                 .arg(mcl_rs::TaskArg::output_slice(&mut out))//the second argument to the kernel is  the output array
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///
    ///```
    pub fn arg(mut self, arg: TaskArg<'a>) -> Self {
        match &arg.data {
            TaskArgData::Scalar(x) => {
                low_level::task_set_arg(self.c_handle, self.curr_arg as u64, x, arg.flags)
            }
            TaskArgData::Buffer(x) => {
                low_level::task_set_arg(self.c_handle, self.curr_arg as u64, x, arg.flags)
            }
            // TaskArgData::Local(x) => {
            //     low_level::task_set_local(self.c_handle, self.curr_arg as u64, *x, arg.flags)
            // }
            #[cfg(feature="shared_mem")]
            TaskArgData::Shared(..) => panic!("must use arg_shared_buffer api "),
            TaskArgData::Empty => panic!("cannot have an empty arg"),
        }
        self.args[self.curr_arg] = TaskArgOrBuf::TaskArg(arg);
        self.curr_arg += 1;

        self
    }

    /// Set a new argument registered buffer `arg` for `this` mcl task in preparation
    ///
    /// Returns the Task with the arg set
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     
    ///     let mut a = vec![0;100];
    ///     let buf = mcl.register_buffer(mcl_rs::TaskArg::inout_slice(&mut a)
    ///            .resident(true)
    ///            .dynamic(true),
    ///     );
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg_buffer(buf)
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///
    ///```
    pub fn arg_buffer(mut self, buffer: RegisteredBuffer<'a>) -> Self {
        low_level::task_set_arg_registered_buffer(self.c_handle, self.curr_arg as u64, &buffer);
        self.args[self.curr_arg] = TaskArgOrBuf::RegBuf(buffer.clone());
        self.curr_arg += 1;

        self
    }

    /// Set a new shared argument buffer `arg` for `this` mcl task in preparation
    ///
    /// Returns the Task with the arg set
    ///
    /// # Saftey
    ///
    /// We track the reader and writers associated with this buffer to protect access, but that only applies to this process
    ///
    /// Regardless of and local saftey guarentees we provide, using a shared buffer is still inherently unsafe as this represents a multi-processed shared memory buffer, managed by the MCL c-library, no guarantees can be provided on
    /// multiple processes modifying this buffer simultaneously
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     
    ///     let num_elems = 100;
    ///     let buf = mcl.create_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>("my_buffer", num_elems));
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///
    ///     let mcl_future = unsafe {
    ///         mcl.task("my_kernel", 1)
    ///             .arg_shared(buf) //potentially accessed by other proccesses hence unsafe
    ///             .exec(pes)
    ///     };
    ///     futures::executor::block_on(mcl_future);
    ///
    ///```
    #[cfg(feature = "shared_mem")]
    pub unsafe fn arg_shared_buffer(mut self, buffer: SharedMemBuffer) -> Self {
        //TODO fix the offset issue
        low_level::task_set_arg_shared_mem_buffer(self.c_handle, self.curr_arg as u64, &buffer);
        self.args[self.curr_arg] = TaskArgOrBuf::ShmBuf(buffer.clone());
        self.curr_arg += 1;

        self
    }

    /// Set the local workgroup size `les` to the mcl task in preparation
    ///
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///```
    pub fn lwsize(mut self, les: [u64; 3]) -> Self {
        self.les = Some(les);

        return self;
    }

    /// Set the preferred device `dev` to the mcl task in preparation
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .lwsize(les)
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///```
    pub fn dev(mut self, dev: DevType) -> Self {
        self.dev = dev;

        return self;
    }

    /// If this is a shared task return the task id otherwise return None
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///     let data = vec![0; 4];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///
    ///     let task = mcl.task("my_kernel", 1);
    ///     assert_eq!(task.shared_id(), None);
    ///
    ///     let shared_task = mcl.shared_task("my_kernel2", 1);
    ///     assert!(shared_task.shared_id().is_some());
    ///```
    pub fn shared_id(&self) -> Option<u32> {
        self.shared_id
    }

    /// Submit the task for execution
    /// This is an asynchronous operation, meaning that no work is actually performed until
    /// the returned future is actualy `await`ed.
    /// While awaiting a task execution, the user application will not make forward progress until
    /// the underylying device has executed the task and performed any necessary data transfers.
    /// Upon return from the await call, any output data is gauranteed to be written to its approriate buffer.
    ///
    /// Task execution order can be enforced by sequentially awaiting tasks, or may be executed simultaneously
    /// using data structures such as Join_all <https://docs.rs/futures/latest/futures/future/fn.join_all.html>
    /// # Examples
    ///     
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let data = vec![0; 4];
    ///     let pes: [u64; 3] = [1, 1, 1];
    ///     let task_1 = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes); //this creates a future we need to await
    ///     let task_2 = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes); //this creates a future we need to await
    ///     let sequential_tasks = async move{
    ///         task_1.await; //task will execute before task 2 is even submitted
    ///         task_2.await;
    ///     }
    ///     futures::executor::block_on(sequential_tasks);
    ///     
    ///     let task_3 = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes); //this creates a future we need to await
    ///     let task_4 = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes); //this creates a future we need to await
    ///     let simultaneous_tasks = futures::future::join_all([task_3,task_4]);
    ///     futures::executor::block_on(simultaneous_tasks); //both tasks submitted "simultaneously" 
    ///```
    pub async fn exec(mut self, ref mut pes: [u64; 3]) {
        assert_eq!(self.curr_arg, self.args.len());
        for arg in &self.args {
            match arg {
                TaskArgOrBuf::RegBuf(buf) => buf.alloc().await,
                #[cfg(feature = "shared_mem")]
                TaskArgOrBuf::ShmBuf(buf) => buf.alloc().await,
                TaskArgOrBuf::TaskArg(_) => {}
            }
        }

        low_level::exec(self.c_handle, pes, &mut self.les, self.dev);

        while low_level::test(self.c_handle) != ReqStatus::Completed {
            async_std::task::yield_now().await;
        }
    }
}

impl Drop for Task<'_> {
    fn drop(&mut self) {
        low_level::task_free(self.c_handle);
    }
}

#[cfg(feature = "shared_mem")]
pub struct SharedTask {
    pid: pid_t,
    hdl_id: u32,
}

/// Represents a shared task handle, that can be use to await the completion of a task on a different process from the one which created it
#[cfg(feature = "shared_mem")]
impl SharedTask {
    pub(crate) fn new(pid: pid_t, hdl_id: u32) -> SharedTask {
        SharedTask { pid, hdl_id }
    }


    /// Await the completetion of the shared task.
    /// This is an asynchronous operation, meaning that no work is actually performed until
    /// the returned future is actualy `await`ed.
    /// While awaiting a task execution, the user application will not make forward progress until
    /// the underylying device has executed the task and performed any necessary data transfers.
    /// Upon return from the await call, any output data is gauranteed to be written to its approriate buffer.
    ///
    /// Task execution order can be enforced be sequentially awaiting tasks, or may be executed simultaneous
    /// using data structures such as Join_all <https://docs.rs/futures/latest/futures/future/fn.join_all.html>
    /// # Examples
    ///     
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let pid = 0; //user is required to set this approriately
    ///     let task_ids = [0,1,2,3]; //user is required to set this approriately
    ///
    ///     let t1 = mcl.attach_shared_task(pid,task_ids[0]);
    ///     let t2 = mcl.attach_shared_task(pid,task_ids[1]);
    ///     let t3 = mcl.attach_shared_task(pid,task_ids[2]);
    ///     let t4 = mcl.attach_shared_task(pid,task_ids[3]);
    ///     
    ///     let tasks = async move {
    ///         t1.await;
    ///         t2.await;
    ///         futures::future::join_all([t3,t4]).await;
    ///     }
    ///     futures::executor::block_on(tasks); 
    ///```
    pub async fn wait(&self) {
        while low_level::shared_task_test(self.pid, self.hdl_id) != ReqStatus::Completed {
            async_std::task::yield_now().await;
        }
    }
}

#[derive(Clone)]
enum TaskArgOrBuf<'a> {
    TaskArg(TaskArg<'a>),
    RegBuf(RegisteredBuffer<'a>),
    #[cfg(feature = "shared_mem")]
    ShmBuf(SharedMemBuffer),
}

impl<'a> Default for TaskArgOrBuf<'a> {
    fn default() -> Self {
        TaskArgOrBuf::TaskArg(Default::default())
    }
}

#[derive(Clone)]
pub(crate) enum TaskArgData<'a> {
    Scalar(&'a [u8]),
    Buffer(&'a [u8]),
    // Local(usize),
    #[cfg(feature="shared_mem")]
    Shared(String, usize),
    Empty,
}

impl<'a> TaskArgData<'a> {
    pub(crate) fn len(&self) -> usize {
        match self {
            TaskArgData::Scalar(x) => x.len(),
            TaskArgData::Buffer(x) => x.len(),
            // TaskArgData::Local(x) => *x,
            #[cfg(feature="shared_mem")]
            TaskArgData::Shared(_, x) => *x,
            TaskArgData::Empty => 0,
        }
    }
}

/// Represents a data argument for an MCL task along with the use flags (e.g. input, output, access type etc.)
#[derive(Clone)]
pub struct TaskArg<'a> {
    pub(crate) data: TaskArgData<'a>,
    pub(crate) flags: ArgOpt,
    pub(crate) orig_type_size: usize,
}

impl<'a> Default for TaskArg<'a> {
    fn default() -> Self {
        TaskArg {
            data: TaskArgData::Empty,
            flags: ArgOpt::EMPTY,
            orig_type_size: 0,
        }
    }
}

fn to_u8_slice<T>(data: &[T]) -> &[u8] {
    let num_bytes = std::mem::size_of::<T>() * data.len();
    unsafe { std::slice::from_raw_parts(data.as_ptr() as *const u8, num_bytes) }
    //no alignment issues going from T to u8 as u8 aligns to everything
}

impl<'a> TaskArg<'a> {
    /// Create a new task input argument from `slice`
    ///
    ///
    /// # Examples
    ///     
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = vec![0; 4];
    ///     let task = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data));
    ///```
    // pub fn input_slice<T: Into<MclBufferDataType<'a>>>(slice: T) -> Self {
    pub fn input_slice<T>(slice: &'a [T]) -> Self {
        TaskArg {
            data: TaskArgData::Buffer(to_u8_slice(slice)),
            flags: ArgOpt::INPUT | ArgOpt::BUFFER,
            orig_type_size: std::mem::size_of::<T>(),
        }
    }

    /// Create a new task input argument from `scalar`
    ///
    ///
    /// # Examples
    ///     
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///     let data = 4;
    ///     let task = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_scalar(&data));
    ///```  
    pub fn input_scalar<T>(scalar: &'a T) -> Self {
        // let slice = std::
        TaskArg {
            data: TaskArgData::Scalar(to_u8_slice(std::slice::from_ref(scalar))),
            flags: ArgOpt::INPUT | ArgOpt::SCALAR,
            orig_type_size: std::mem::size_of::<T>(),
        }
    }
    // /// Requests an allocation of `num_bytes`
    // ///
    // ///
    // /// # Examples
    // ///     
    // ///```no_run
    // ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    // ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    // ///         .with_compile_args("-D MYDEF").load();
    // ///     let data = 4;
    // ///     let les: [u64; 3] = [1, 1, 1];
    // ///     let pes: [u64; 3] = [1, 1, 1];
    // ///     let task = mcl.task("my_kernel", 1)
    // ///                 .arg(mcl_rs::TaskArg::input_local(400));
    // ///```  
    // pub fn input_local(num_bytes: usize) -> Self {
    //     TaskArg {
    //         data: TaskArgData::Local(num_bytes),
    //         flags: ArgOpt::LOCAL,
    //         orig_type_size: 1,
    //     }
    // }

    /// Create an new task input argument using a shared memory buffer
    ///
    /// # Examples
    ///     
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///
    ///     let num_elems = 100;
    ///     let buffer = mcl.create_shared_buffer(mcl_rs::TaskArg::input_shared::<f32>("my_buffer",num_elems).resident(true));
    ///     let task = mcl.task("my_kernel",1)
    ///                   .input_shared(buffer);
    ///```  
    #[cfg(feature="shared_mem")]
    pub fn input_shared<T>(name: &str, size: usize) -> Self {
        TaskArg {
            data: TaskArgData::Shared(name.to_owned(), size * std::mem::size_of::<T>()),
            flags: ArgOpt::SHARED | ArgOpt::INPUT | ArgOpt::BUFFER,
            orig_type_size: std::mem::size_of::<T>(),
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::output_slice(&mut data));
    ///```
    pub fn output_slice<T>(slice: &'a mut [T]) -> Self {
        TaskArg {
            data: TaskArgData::Buffer(to_u8_slice(slice)),
            flags: ArgOpt::OUTPUT | ArgOpt::BUFFER,
            orig_type_size: std::mem::size_of::<T>(),
        }
    }

    /// Create an new task output argument using a shared memory buffer
    ///
    /// # Examples
    ///     
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///
    ///     let num_elems = 100;
    ///     let buffer = mcl.create_shared_buffer(mcl_rs::TaskArg::output_shared::<f32>("my_buffer",num_elems).resident(true));
    ///     let task = mcl.task("my_kernel",1)
    ///                   .output_shared(buffer);
    ///```  
    #[cfg(feature="shared_mem")]
    pub fn output_shared<T>(name: &str, size: usize) -> Self {
        TaskArg {
            data: TaskArgData::Shared(name.to_owned(), size * std::mem::size_of::<T>()),
            flags: ArgOpt::SHARED | ArgOpt::OUTPUT | ArgOpt::BUFFER,
            orig_type_size: std::mem::size_of::<T>(),
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::output_scalar(&mut data));
    ///```
    pub fn output_scalar<T>(scalar: &'a T) -> Self {
        //mcl expects all outputs to be buffers but we want a nice consistent interface here!
        TaskArg {
            data: TaskArgData::Buffer(to_u8_slice(std::slice::from_ref(scalar))),
            flags: ArgOpt::OUTPUT | ArgOpt::BUFFER,
            orig_type_size: std::mem::size_of::<T>(),
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::inout_slice(&mut data));
    ///```
    pub fn inout_slice<T>(slice: &'a [T]) -> Self {
        TaskArg {
            data: TaskArgData::Buffer(to_u8_slice(slice)),
            flags: ArgOpt::OUTPUT | ArgOpt::INPUT | ArgOpt::BUFFER,
            orig_type_size: std::mem::size_of::<T>(),
        }
    }

    /// Create an new task input+output argument using a shared memory buffer
    ///
    /// # Examples
    ///     
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.create_prog("my_prog",mcl_rs::PrgType::Src)
    ///         .with_compile_args("-D MYDEF").load();
    ///
    ///     let num_elems = 100;
    ///     let buffer = mcl.create_shared_buffer(mcl_rs::TaskArg::inout_shared::<f32>("my_buffer",num_elems).resident(true));
    ///     let task = mcl.task("my_kernel",1)
    ///                   .inout_shared(buffer);
    ///```  
    #[cfg(feature="shared_mem")] 
    pub fn inout_shared<T>(name: &str, size: usize) -> Self {
        // println!("inout_shared: {size} {}",size*std::mem::size_of::<T>());
        TaskArg {
            data: TaskArgData::Shared(name.to_owned(), size * std::mem::size_of::<T>()),
            flags: ArgOpt::SHARED | ArgOpt::INPUT | ArgOpt::OUTPUT | ArgOpt::BUFFER,
            orig_type_size: std::mem::size_of::<T>(),
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::inout_scalar(&mut data));
    ///```
    pub fn inout_scalar<T>(scalar: &'a T) -> Self {
        TaskArg {
            data: TaskArgData::Buffer(to_u8_slice(std::slice::from_ref(scalar))),
            flags: ArgOpt::OUTPUT | ArgOpt::INPUT | ArgOpt::BUFFER,
            orig_type_size: std::mem::size_of::<T>(),
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).resident(true))
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///```
    pub fn resident(mut self, val: bool) -> Self {
        if val {
            self.flags = self.flags | ArgOpt::RESIDENT;
        } else {
            self.flags = self.flags & !ArgOpt::RESIDENT;
        }
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).dynamic(true))
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///```
    pub fn dynamic(mut self, val: bool) -> Self {
        if val {
            self.flags = self.flags | ArgOpt::DYNAMIC;
        } else {
            self.flags = self.flags & !ArgOpt::DYNAMIC;
        }
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).done(true))
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///```  
    pub fn done(mut self, val: bool) -> Self {
        if val {
            self.flags = self.flags | ArgOpt::DONE;
        } else {
            self.flags = self.flags & !ArgOpt::DONE;
        }
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).invalid(true))
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///```  
    pub fn invalid(mut self, val: bool) -> Self {
        if val {
            self.flags = self.flags | ArgOpt::INVALID;
        } else {
            self.flags = self.flags & !ArgOpt::INVALID;
        }
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).read_only(true))
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///```  
    pub fn read_only(mut self, val: bool) -> Self {
        if val {
            self.flags = self.flags | ArgOpt::RDONLY;
        } else {
            self.flags = self.flags & !ArgOpt::RDONLY;
        }
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
    ///     let mcl_future = mcl.task("my_kernel", 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data).write_only(true))
    ///                 .exec(pes);
    ///     futures::executor::block_on(mcl_future);
    ///```
    pub fn write_only(mut self, val: bool) -> Self {
        if val {
            self.flags = self.flags | ArgOpt::WRONLY;
        } else {
            self.flags = self.flags & !ArgOpt::WRONLY;
        }
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

#[cfg(feature = "versal")]
pub struct TaskBinProps {
    c_handle: mcl_bin_properties,
}

#[cfg(feature = "versal")]
impl TaskBinProps {
    pub fn new(num_devices: u64, types: u64, name: &str) -> Self {
        TaskBinProps {
            c_handle: mcl_bin_properties {
                devices: num_devices,
                types: types,
                name: CString::new(name).unwrap().into_raw(),
            },
        }
    }

    pub fn get_devices(&self) -> u64 {
        self.c_handle.devices
    }

    pub fn get_types(&self) -> u64 {
        self.c_handle.types
    }

    pub fn get_name(&self) -> String {
        return unsafe {
            CString::from_raw(self.c_handle.name as *mut _)
                .into_string()
                .unwrap()
        };
    }
}
