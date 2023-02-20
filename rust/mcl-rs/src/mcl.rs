#![cfg_attr(all(doc, CHANNEL_NIGHTLY), feature(doc_auto_cfg))]

use crate::low_level;
use crate::low_level::{TaskOpt};
#[cfg(feature = "shared_mem")]
use crate::low_level::{ArgOpt};


// use crate::device::DevInfo;
use crate::prog::{PrgType, Prog};
use crate::registered_buffer::RegisteredBuffer;
#[cfg(feature = "shared_mem")]
use crate::registered_buffer::SharedMemBuffer;
use crate::task::{ Task, TaskArg};
#[cfg(feature = "shared_mem")]
use crate::task::SharedTask;
use crate::transfer::Transfer;

use bitflags::bitflags;

bitflags! {
    #[derive(Default)]
    pub(crate) struct MclEnvBuilderFlags: u64 {
        const MCL_SET_BIND_WORKERS = 0x01 as u64;
    }
}

/// This structure is used to setup the MCL environment with the given parameters
/// # Examples
///```
/// use mcl_rs::MclEnvBuilder;
///
/// let env = MclEnvBuilder::new()
///                 .num_workers(10)
///                 .bind_workers()
///                 .initialize();
///```
pub struct MclEnvBuilder {
    num_workers: usize,
    flags: MclEnvBuilderFlags,
}

impl MclEnvBuilder {
    /// Creates and returns a new MclEnvBuilder with the default values
    ///
    /// # Examples
    ///```
    /// use mcl_rs::MclEnvBuilder;
    ///
    /// let env = MclEnvBuilder::new()
    ///                 .initialize();
    ///```
    pub fn new() -> MclEnvBuilder {
        MclEnvBuilder {
            num_workers: 1,
            flags: Default::default(),
        }
    }

    /// Set the num_workers to pass to the mcl initialization function
    ///
    ///
    /// # Examples
    ///```
    /// use mcl_rs::MclEnvBuilder;
    ///
    /// let env = MclEnvBuilder::new()
    ///                 .num_workers(1);
    ///```
    pub fn num_workers(mut self, workers: usize) -> MclEnvBuilder {
        assert!(workers > 0);

        self.num_workers = workers;

        self
    }

    /// Bind worker threads to their own core
    ///
    /// # Examples
    ///```
    /// use mcl_rs::MclEnvBuilder;
    ///
    /// let env = MclEnvBuilder::new()
    ///                 .bind_workers();
    ///```
    pub fn bind_workers(mut self) -> MclEnvBuilder {
        assert!(self.num_workers > 0);

        self.flags |= MclEnvBuilderFlags::MCL_SET_BIND_WORKERS;

        self
    }

    /// Initializes mcl
    ///
    /// Returns an [Mcl] instance
    ///  
    /// # Examples
    ///```
    /// use mcl_rs::MclEnvBuilder;
    ///
    /// let env = MclEnvBuilder::new()
    ///                 .initialize();
    ///```
    pub fn initialize(self) -> Mcl {
        low_level::init(self.num_workers as u64, self.flags.bits());

        Mcl { _env: MclEnv }
    }
}

/// Represents an initialize MCL environment. When this struct goes out of scope the MCL environment is finalized.
/// Thus, there is no need to explicitly call the equivalent of the (c-api) mcl_finit()
pub(crate) struct MclEnv;


impl Drop for MclEnv {
    /// Finalizes mcl when MclEnv goes out of scope
    fn drop(&mut self) {
        low_level::finit();
    }
}

/// Represents an initialize MCL environment. When this struct goes out of scope the MCL environment is finalized.
/// Thus, there is no need to explicitly call the equivalent of (c-api) mcl_finit()
pub struct Mcl {
    pub(crate) _env: MclEnv,
}

impl Mcl {
    /// Creates a new mcl prog from the given source file pointed to by `prog_path` and the specified Program Type [crate::prog::PrgType].
    ///
    /// Returns a new [Prog][crate::prog::Prog] that can be loaded into the current mcl environment (that is [load()][crate::prog::Prog::load] will need to be called on it at a later time).
    ///
    /// # Examples
    ///```no_run
    /// use mcl_rs::{MclEnvBuilder,PrgType};
    ///
    /// let mcl = MclEnvBuilder::new().num_workers(10).initialize();
    /// mcl.create_prog("my_path",PrgType::Src);
    ///```
    pub fn create_prog(&self, prog_path: &str, prog_type: PrgType) -> Prog {
        Prog::from(prog_path, prog_type)
    }

    /// Creates and loads a new mcl prog from the given source file pointed to by `prog_path` and the specified Program Type [crate::prog::PrgType].
    ///
    /// This is a convenience function for when no additional arguments need to be supplied with program/kernel during compile time
    ///
    /// # Examples
    ///```no_run
    /// use mcl_rs::{MclEnvBuilder,PrgType};
    ///
    /// let mcl = MclEnvBuilder::new().num_workers(10).initialize();
    /// mcl.load_prog("my_path",PrgType::Src);
    ///```
    pub fn load_prog(&self, prog_path: &str, prog_type: PrgType) {
        Prog::from(prog_path, prog_type).load()
    }

    /// Creates a new mcl task from the given kernel.
    ///
    /// This kernel must exist in a previously loaded [Prog][crate::prog::Prog].
    ///
    /// Returns a new Task representing this kernel
    /// # Examples
    ///
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///
    ///     let t = mcl.task("my_kernel", 2);
    ///```
    pub fn task<'a>(&self, kernel_name_cl: &str, nargs: usize) -> Task<'a> {
        Task::new(kernel_name_cl, nargs, TaskOpt::EMPTY)
    }

    /// Creates a new mcl shared task from the given kernel.
    ///
    /// This kernel must exist in a previously loaded [Prog][crate::prog::Prog].
    ///
    /// Other processes will be able to attach to this task by its shared task ID
    /// i.e. [crate::task::Task::shared_id]
    ///
    /// Returns a new Task representing this kernel
    /// # Examples
    ///
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///
    ///     let t = mcl.shared_task("my_kernel", 2);
    ///```
    #[cfg(feature = "shared_mem")]
    pub fn shared_task<'a>(&self, kernel_name_cl: &str, nargs: usize) -> Task<'a> {
        Task::new(kernel_name_cl, nargs, TaskOpt::SHARED)
    }

    /// Creates a new mcl shared task from the given process id and unique task id.
    ///
    /// This task must have been created by process `pid`
    /// # Examples
    ///
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let pid = 0; //user is required to set this approriately
    ///     let task_id = 0; //user is required to set this approriately
    ///
    ///     let t = mcl.attach_shared_task(pid,task_id);
    ///```
    #[cfg(feature = "shared_mem")]
    pub fn attach_shared_task(&self, pid: i32, task_id: u32) -> SharedTask {
        SharedTask::new(pid, task_id)
    }

    /// Creates a new mcl transfer task with that will transfer `nargs` and suggest to the scheduler that `ncopies` should be created
    /// # Examples
    ///```no_run     
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///
    ///     let tr = mcl.transfer(1, 1);
    ///```
    pub fn transfer<'a>(&self, nargs: usize, ncopies: usize) -> Transfer<'a> {
        Transfer::new(nargs, ncopies, 0)
    }

    /// Register the provided `arg` as a buffer for future use with MCL resident memory
    ///
    /// # Panics
    ///
    /// This call will panic if the provided TaskArg was not created from one of the slice variants:
    /// [input_slice][crate::task::TaskArg::input_slice]
    /// [output_slice][crate::task::TaskArg::output_slice]
    /// [inout_slice][crate::task::TaskArg::inout_slice]
    ///
    /// Further this call will also panic if the argument [residient][crate::task::TaskArg::resident()] property was not set to true.
    ///
    /// # Examples
    ///```no_run     
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///
    ///     let mut a = vec![1;100];
    ///     let buffer = mcl.register_buffer(mcl_rs::TaskArg::inout_slice(& mut a).resident(true));
    ///```
    pub fn register_buffer<'a>(&self, arg: TaskArg<'a>) -> RegisteredBuffer<'a> {
        RegisteredBuffer::new(arg)
    }

    /// Creates a buffer that can be accessed via shared memory from multiple processes.
    /// If using only the "shared_mem" feature this buffer will be shared only in host memory.
    /// If the "pocl_extensions" feature is enabled, and the patched version of POCL 1.8 has been succesfully
    /// installed (please see <https://github.com/pnnl/mcl/tree/dev#using-custom-pocl-extensions> for more information)
    ///
    /// # Panics
    ///
    /// This call will panic if the provided TaskArg was not created from one of the shared variants:
    /// [input_shared][crate::task::TaskArg::input_shared]
    /// [output_shared][crate::task::TaskArg::output_shared]
    /// [inout_shared][crate::task::TaskArg::inout_shared]
    ///
    ///
    /// # Examples
    ///```no_run     
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///     let num_elems = 100;
    ///     let buffer = mcl.create_shared_buffer(mcl_rs::TaskArg::inout_shared::<f32>("my_buffer",num_elems).resident(true));
    ///```
    #[cfg(feature = "shared_mem")]
    pub fn create_shared_buffer(&self, mut arg: TaskArg<'_>) -> SharedMemBuffer {
        arg.flags |= ArgOpt::SHARED_MEM_NEW
            | ArgOpt::SHARED_MEM_DEL_OLD
            | ArgOpt::DYNAMIC
            | ArgOpt::RESIDENT;
        // println!("{:?}",arg.flags);
        SharedMemBuffer::new(arg)
    }


    /// Attaches to a shared buffer that was previously created by another process
    /// If using only the "shared_mem" feature this buffer will be shared only in host memory.
    /// If the "pocl_extensions" feature is enabled, and the patched version of POCL 1.8 has been succesfully
    /// installed (please see <https://github.com/pnnl/mcl/tree/dev#using-custom-pocl-extensions> for more information)
    ///
    /// The provided `TaskArg` should have been constructed with identical arguments to the original buffer
    ///
    /// # Panics
    ///
    /// This call will panic if the provided TaskArg was not created from one of the shared variants:
    /// [input_shared][crate::task::TaskArg::input_shared]
    /// [output_shared][crate::task::TaskArg::output_shared]
    /// [inout_shared][crate::task::TaskArg::inout_shared]
    ///
    ///
    /// # Examples
    ///```no_run     
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///     let num_elems = 100;
    ///
    ///     let buffer = mcl.attach_shared_buffer(mcl_rs::TaskArg::inout_shared::<f32>("my_buffer",num_elems).resident(true));
    ///```
    #[cfg(feature = "shared_mem")]
    pub fn attach_shared_buffer(&self, mut arg: TaskArg<'_>) -> SharedMemBuffer {
        arg.flags |= ArgOpt::DYNAMIC | ArgOpt::RESIDENT;
        SharedMemBuffer::new(arg)
    }


    /// Returns the info of the device specified by `id`
    ///
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///
    ///     let t = mcl.task("my_kernel", 2);
    ///```
    pub fn get_dev(&self, id: u32) -> crate::DevInfo {
        low_level::get_dev(id)
    }

    /// Return the number of devices in the system
    ///
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     let num_dev = mcl.get_ndev();
    ///```
    pub fn get_ndev(&self) -> u32 {
        low_level::get_ndev()
    }
}
