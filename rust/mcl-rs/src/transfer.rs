use crate::device::DevType;
use crate::low_level;
use crate::low_level::ReqStatus;
use crate::task::{TaskArg, TaskArgData};

use libmcl_sys::mcl_transfer;

/// Transfer can be used to create a request for data transfer from MCL.
pub struct Transfer<'a> {
    args: Vec<TaskArg<'a>>,
    curr_arg: usize,
    d_type: DevType,
    c_handle: *mut mcl_transfer,
}

impl<'a> Transfer<'a> {
    /// Creates a new transfer with the given parameters
    ///
    /// ## Arguments
    /// * `num_args` - The number of arguments that will be transfered
    /// * `ncopies` - The number of copies to create
    /// * `flags` - Other related flags
    ///
    /// Returns a new  Transfer object
    pub(crate) fn new(num_args: usize, ncopies: usize, flags: u64) -> Self {
        Transfer {
            args: vec![Default::default(); num_args],
            curr_arg: 0,
            d_type: DevType::ANY,
            c_handle: low_level::transfer_create(num_args as u64, ncopies as u64, flags),
        }
    }

    /// Adds an argument to be transferred by this request
    ///
    /// ## Arguments
    /// * ` arg` - The argument to be transferred enclosed in a mcl_rs::TaskArg
    ///
    /// Returns the Transfer object
    ///
    /// # Examples
    ///```no_run     
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///     
    ///     let data = vec![0; 4];
    ///
    ///     let tr = mcl.transfer(1, 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data));
    ///```
    pub fn arg(mut self, arg: TaskArg<'a>) -> Self {
        match &arg.data {
            TaskArgData::Scalar(x) => {
                low_level::transfer_set_arg(self.c_handle, self.curr_arg as u64, x, 0, arg.flags)
            }
            TaskArgData::Buffer(x) => {
                low_level::transfer_set_arg(self.c_handle, self.curr_arg as u64, x, 0, arg.flags)
            }
            TaskArgData::Local(x) => {
                low_level::transfer_set_local(self.c_handle, self.curr_arg as u64, *x, 0, arg.flags)
            }
            #[cfg(feature="shared_mem")]
            TaskArgData::Shared(..) => panic!("must use arg_shared_buffer api "),
            TaskArgData::Empty => panic!("cannot have an empty arg"),
        }
        self.args[self.curr_arg] = arg;
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
    /// # Examples
    ///```no_run     
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///
    ///     let data = vec![0; 4];
    ///
    ///     let tr = mcl.transfer(1, 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .dev(mcl_rs::DevType::CPU);
    ///```
    pub fn dev(mut self, d_type: DevType) -> Self {
        self.d_type = d_type;
        self
    }

    /// Submit the transfer request
    ///
    /// Returns a TransferHandle that can be queried for completion
    ///
    /// # Examples
    ///```no_run     
    ///     let mcl = mcl_rs::MclEnvBuilder::new().initialize();
    ///     mcl.load_prog("my_prog",mcl_rs::PrgType::Src);
    ///
    ///     let data = vec![0; 4];
    ///
    ///     let t_hdl = mcl.transfer(1, 1)
    ///                 .arg(mcl_rs::TaskArg::input_slice(&data))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec();
    ///     futures::executor::block_on(t_hdl);
    ///```
    pub async fn exec(self) {
        assert_eq!(self.curr_arg, self.args.len());
        low_level::transfer_exec(self.c_handle, self.d_type);

        while low_level::transfer_test(self.c_handle) != ReqStatus::Completed {
            async_std::task::yield_now().await;
        }
    }
}

impl Drop for Transfer<'_> {
    fn drop(&mut self) {
        low_level::transfer_free(self.c_handle);
    }
}
