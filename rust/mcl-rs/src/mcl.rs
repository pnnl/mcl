use crate::low_level;

// use crate::device::DevInfo;
use crate::prog::{Prog,PrgType};
use crate::task::{InnerTask,Task};

use bitflags::bitflags;

bitflags! {
    #[derive(Default)]
    pub(crate) struct MclEnvBuilderFlags: u64 {
        const MCL_SET_BIND_WORKERS = 0x01 as u64;
    }
}

/// This structure is used to setup the MCL environment with the given parameters
/// # Example
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
    /// # Example
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
    /// ## Arguments
    /// 
    /// * `num_workers` - The num_workers to pass to MCL
    /// 
    /// Returns the MclEnvBuilder with the num_workers set
    /// 
    /// # Example
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

    /// Set the num_workers to pass to the mcl initialization function
    /// 
    /// ## Arguments
    /// 
    /// * `num_workers` - The num_workers to pass to MCL
    /// 
    /// Returns the MclEnvBuilder with the num_workers set
    /// 
    /// # Example
    ///``` 
    /// use mcl_rs::MclEnvBuilder;
    /// 
    /// let env = MclEnvBuilder::new()
    ///                 .bind_workers();
    ///``` 
    pub fn bind_workers(mut self) -> MclEnvBuilder {

        assert!( self.num_workers > 0);

        self.flags |= MclEnvBuilderFlags::MCL_SET_BIND_WORKERS;

        self
    }

    /// Initializes mcl
    /// 
    /// Returns an [Mcl] instance
    ///  
    /// # Example
    ///``` 
    /// use mcl_rs::MclEnvBuilder;
    /// 
    /// let env = MclEnvBuilder::new()
    ///                 .initialize();
    ///```
    pub fn initialize(self) -> Mcl {

        low_level::init(self.num_workers as u64, self.flags.bits());

        Mcl{_env: MclEnv}
    }
}

/// Represents an initialize MCL environment. When this struct goes out of scope the MCL environment is finalized.
/// Thus, there is no need to explicitly call the equivalent of mcl_finit()
pub(crate) struct MclEnv;

// impl MclEnv {
//     fn get_ndev(&self) -> usize {
//         return  low_level::get_ndev() as usize;
//     }
//     pub fn get_dev(&self, id: usize) -> DevInfo {
    
//         low_level::get_dev(id as u32)
//     }
// }


impl Drop for MclEnv {

    /// Finalizes mcl when MclEnv goes out of scope
    fn drop(&mut self) {
        low_level::finit();
    }
}

/// Represents an initialize MCL environment. When this struct goes out of scope the MCL environment is finalized.
/// Thus, there is no need to explicitly call the equivalent of mcl_finit()
pub struct Mcl{
    pub(crate) _env: MclEnv
}

impl Mcl{

    /// Creates a new mcl prog from the given source file pointed to by `prog_path` and the specified Program Type [crate::prog::PrgType].
    /// 
    /// Returns a new [Prog][crate::prog::Prog] that can be loaded into the current mcl environment
    /// 
    /// # Example
    ///```no_run
    /// use mcl_rs::{MclEnvBuilder,PrgType};
    ///
    /// let mcl = MclEnvBuilder::new().num_workers(10).initialize();
    /// mcl.create_prog("my_path",PrgType::Src);
    ///```
    pub fn create_prog(&self, prog_path: &str,prog_type: PrgType) -> Prog {
        Prog::from( prog_path,prog_type)
    }


    /// Creates a new mcl task from the given kernel.
    ///
    /// This kernel must exist in a previously loaded [Prog][crate::prog::Prog].
    /// 
    /// Returns a new Task representing this kernel
    /// # Example
    /// 
    ///     let t = Task::from("my_kernel", 2);
    /// 
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.prog("my_path", "my_comperter_args", PrgFlag::Src);
    ///
    ///     let t = mcl.task("my_kernel", 2);
    ///```
    pub fn task(&self, kernel_name_cl: &str, nargs: usize) -> Task{
        InnerTask::from( kernel_name_cl, nargs).compile()
    }

    /// Get the info of a specific device
    /// 
    /// ## Arguments
    /// 
    /// * `id` - The ID of the device to retrieve info for
    /// 
    /// Returns the info of specificed device
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     let 
    ///
    ///     let t = mcl.task("my_kernel", 2);
    ///```
    pub fn get_dev(&self, id: u32) -> crate::DevInfo {
        low_level::get_dev(id)
    }

    /// Get the number of devices in the system
    /// 
    /// Returns the number of devices available
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     let num_dev = mcl.get_ndev();
    ///```
    pub fn get_ndev(&self) -> u32 {
        low_level::get_ndev()
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
    pub fn wait_all(&self) {
        low_level::wait_all()
        
    }

}


