use libmcl_sys::*;
use crate::low_level;
use crate::device::DevType;
use crate::task::{TaskArg,TaskArgData};

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
            TaskArgData::Mutable(mut x) => low_level::transfer_set_arg_mut(self.hdl.c_handle, self.curr_arg as u64, &mut x, 0, arg.flags),
            TaskArgData::Immutable(x) => low_level::transfer_set_arg(self.hdl.c_handle, self.curr_arg as u64, x, 0, arg.flags),
            TaskArgData::NoData(x) => low_level::transfer_set_local(self.hdl.c_handle, self.curr_arg as u64, x, 0, arg.flags)
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