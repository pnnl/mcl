#![cfg_attr(all(doc, CHANNEL_NIGHTLY), feature(doc_auto_cfg))]
use crate::low_level;
use crate::low_level::ArgOpt;
use crate::task::{TaskArg, TaskArgData};

use std::collections::{BTreeMap, Bound};
use std::ffi::c_void;
use std::ops::RangeBounds;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

struct Alloc {
    size: usize,
    currently_allocated: BTreeMap<usize, usize>,
}

impl Alloc {
    fn new(size: usize) -> Self {
        Alloc {
            size: size,
            currently_allocated: BTreeMap::new(),
        }
    }

    fn try_alloc(&mut self, start_i: usize, end_i: usize) -> Result<(), ()> {
        if end_i > self.size {
            panic!("out of bounds error on mcl buffer");
        }
        if self.currently_allocated.is_empty() {
            //no other allocations yet
            self.currently_allocated.insert(start_i, end_i);
            // println!("{:?}",self.currently_allocated);
            return Ok(());
        } else {
            //other allocations exist
            if self.currently_allocated.contains_key(&start_i) {
                //something already allocated from same index
                Err(())
            } else {
                //no other allocations use the same start index, but I may exist with another allocation
                let mut iter = self.currently_allocated.range(start_i..end_i);
                if let Some(next_lowest) = iter.clone().rev().next() {
                    // check the closest allocation that starts below me, or there are no allocations lower than me
                    if start_i < *next_lowest.1 {
                        // i start before this allocation ends
                        return Err(());
                    }
                }
                //at this point we know start_i doesnt exist in a previous allocation now check end_i
                if let Some(_) = iter.next() {
                    //we know some other allocation exists between start_i and end_i
                    return Err(());
                }
                //woohoo we can allocate!
                self.currently_allocated.insert(start_i, end_i);
                // println!("{:?}",self.currently_allocated);
                return Ok(());
            }
        }
    }

    fn free(&mut self, start_i: usize, end_i: usize) {
        if let Some(v) = self.currently_allocated.remove(&start_i) {
            assert_eq!(
                v, end_i,
                "unexepected subbuffer end index: {start_i} {v} {end_i}"
            )
        } else {
            // println!("{:?}",self.currently_allocated);
            panic!("unexepected subbuffer start index: {start_i}")
        }
    }
}

struct BufferMetaData {
    orig_type_size: usize,
    offset: usize,
    len: usize,
    cnt: Arc<AtomicUsize>,
    my_alloc: Arc<Mutex<Alloc>>,
    parent_alloc: Option<Arc<Mutex<Alloc>>>,
    alloced: Arc<AtomicBool>,
}

/// Represents an MCL registered buffer, which is essentially a pointer
/// to data which exists in device Resident memory. This allows multiple
/// tasks to use the same buffer. Further, we support creating sub-buffers
/// of a registered buffer to alleviate some  of the overhead associated with
/// creating new buffers.
///
/// #Safety
/// Given that RegisteredBuffers can be used by multiple task simultaneously, and
/// that accelerators are often multi-threaded we need to ensure that RegisteredBuffers
/// are safe with respect to read and write access.
///
/// Internal to each RegisteredBuffer, is an "allocator" which keeps track of sub buffers
/// that have been created so that it is not possible to simultaneously have two subuffers
/// that overlap one other and potentially modify the overlapping contents.
///
/// A task will delay executing, until it is able to "allocate" its subbuffer.
///
/// Currently this is a  mutally exclusive allocation, regardless of if the sub buffer
/// only requires read access vs write access. This unfortunately serializes readonly tasks
/// using overlapping regions of the RegisteredBuffer. We are working on a better allocator
/// to relax this restriction.
///
/// Finally, RegisteredBuffers are reference counted objects, and will automatically free the aquired MCL resources
/// once the last reference is dropped.
///
pub struct RegisteredBuffer<'a> {
    data: TaskArg<'a>,
    meta: BufferMetaData,
}

impl<'a> Clone for RegisteredBuffer<'a> {
    fn clone(&self) -> Self {
        self.meta.cnt.fetch_add(1, Ordering::SeqCst);
        RegisteredBuffer {
            data: self.data.clone(),
            meta: BufferMetaData {
                orig_type_size: self.meta.orig_type_size,
                offset: self.meta.offset,
                len: self.meta.len,
                cnt: self.meta.cnt.clone(),
                my_alloc: self.meta.my_alloc.clone(),
                parent_alloc: self.meta.parent_alloc.clone(),
                alloced: self.meta.alloced.clone(),
            },
        }
    }
}

impl<'a> Drop for RegisteredBuffer<'a> {
    fn drop(&mut self) {
        // println!("dropping {} {} {}",self.meta.offset,self.meta.len,self.meta.cnt.load(Ordering::SeqCst));
        if self.meta.cnt.fetch_sub(1, Ordering::SeqCst) == 1 {
            match &self.data.data {
                TaskArgData::Buffer(x) => low_level::unregister_buffer(x),
                _ => unreachable!("Can not have a Registered Buffer that is not a buffer"),
            }
            if self.meta.alloced.load(Ordering::SeqCst) {
                if let Some(p_alloc) = &self.meta.parent_alloc {
                    //we are a sub array!
                    p_alloc
                        .lock()
                        .unwrap()
                        .free(self.meta.offset, self.meta.offset + self.meta.len);
                } else {
                    //we are not a sub array
                    self.meta
                        .my_alloc
                        .lock()
                        .unwrap()
                        .free(self.meta.offset, self.meta.offset + self.meta.len);
                }
                self.meta.alloced.store(false, Ordering::SeqCst);
            }
        }
    }
}

impl<'a> RegisteredBuffer<'a> {
    pub(crate) fn new(data: TaskArg<'a>) -> Self {
        let orig_type_size = data.orig_type_size;
        let len = data.data.len();
        // println!("dots: {orig_type_size} len: {len}");
        match &data.data {
            TaskArgData::Scalar(_) => panic!("cannot register a scalar"),
            TaskArgData::Buffer(x) => low_level::register_buffer(x, data.flags),
            // TaskArgData::Local(_) => panic!("Must register a buffer"),
            #[cfg(feature = "shared_mem")]
            TaskArgData::Shared(..) => {
                panic!("Use shared_buffer the create/attach to a shared buffer")
            }
            TaskArgData::Empty => panic!("cannot have an empty arg"),
        }
        RegisteredBuffer {
            data: data,
            meta: BufferMetaData {
                orig_type_size: orig_type_size,
                offset: 0,
                len: len,
                cnt: Arc::new(AtomicUsize::new(1)),
                my_alloc: Arc::new(Mutex::new(Alloc::new(len))),
                parent_alloc: None,
                alloced: Arc::new(AtomicBool::new(false)),
            },
        }
    }

    pub(crate) fn base_addr(&self) -> *mut c_void {
        match &self.data.data {
            TaskArgData::Buffer(x) => x.as_ptr() as *mut c_void,
            _ => unreachable!("Can not have a Registered Buffer that is not a buffer"),
        }
    }

    pub(crate) fn u8_offset(&self) -> i64 {
        self.meta.offset as i64
    }

    pub(crate) fn u8_len(&self) -> u64 {
        self.meta.len as u64
    }

    pub(crate) fn flags(&self) -> ArgOpt {
        self.data.flags
    }

    ///Return to offset into the original RegisteredBuffer this handle starts at.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let mut a = vec![0;100];
    ///     let buf = mcl.register_buffer(mcl_rs::TaskArg::inout_slice(&mut c)
    ///            .resident(true)
    ///            .dynamic(true),
    ///     );
    ///     assert_eq!(buf.offset(),0);
    ///     let sub_buf = buf.sub_buffer(10..20);
    ///     assert_eq!(sub_buf.offset(),10);
    ///```
    pub fn offset(&self) -> usize {
        self.meta.offset / self.meta.orig_type_size
    }

    ///Return to len of the Registered(sub)Buffer
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let mut a = vec![0;100];
    ///     let buf = mcl.register_buffer(mcl_rs::TaskArg::inout_slice(&mut c)
    ///            .resident(true)
    ///            .dynamic(true),
    ///     );
    ///     assert_eq!(buf.len(),100);
    ///     let sub_buf = buf.sub_buffer(10..20);
    ///     assert_eq!(sub_buf.len(),10);
    ///```
    pub fn len(&self) -> usize {
        self.meta.len / self.meta.orig_type_size
    }

    /// Tries to unregistered a previously registered buffer.
    /// This will only succeed if this is the last reference to the registered buffer.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let mut a = vec![0;100];
    ///     let buf = mcl.register_buffer(mcl_rs::TaskArg::inout_slice(&mut c)
    ///            .resident(true)
    ///            .dynamic(true),
    ///     );
    ///     let sub_buf = buf.sub_buffer(10..20);
    ///     let buf = buf.try_unregister();
    ///     assert!(buf.is_err());
    ///     let buf = buf.unwrap_err();
    ///     drop(sub_buf);
    ///     assert!(buf.try_unregister().is_ok())
    ///```
    pub fn try_unregister(self) -> Result<(), Self> {
        if let Ok(_) = self
            .meta
            .cnt
            .compare_exchange(1, 0, Ordering::SeqCst, Ordering::SeqCst)
        {
            match &self.data.data {
                TaskArgData::Buffer(x) => low_level::unregister_buffer(x),
                _ => unreachable!("Can not have a Registered Buffer that is not a buffer"),
            }
            Ok(())
        } else {
            Err(self)
        }
    }

    /// Tries to invalidate a previously registered buffer.
    /// meaning that the data on the host has changed and needs to be recopied to the device (generally when the buffer is used for input);
    /// This will only succeed if this is the last reference to the registered buffer.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let mut a = vec![0;100];
    ///     let buf = mcl.register_buffer(mcl_rs::TaskArg::inout_slice(&mut c)
    ///            .resident(true)
    ///            .dynamic(true),
    ///     );
    ///     let sub_buf = buf.sub_buffer(10..20);
    ///     assert!(buf.try_invalidate().is_err());
    ///     drop(sub_buf);
    ///     assert!(buf.try_invalidate().is_ok())
    ///```
    pub fn try_invalidate(&self) -> bool {
        if self.meta.cnt.fetch_sub(1, Ordering::SeqCst) == 1 {
            match &self.data.data {
                TaskArgData::Buffer(x) => low_level::invalidate_buffer(x),
                _ => unreachable!("Can not have a Registered Buffer that is not a buffer"),
            }
            true
        } else {
            false
        }
    }

    /// Creates a sub buffer using the provided range from a given RegisteredBuffer
    /// The sub buffer essentially "locks" the elements in the provided range
    /// delaying other sub buffers executing with overlapping elements until all references to this sub buffer
    /// has been dropped.
    /// Note that sub buffer element locking happens at task execution time rather that sub buffer handle creation.
    /// This allows overlapping sub buffers be created and passed as arguments to different tasks, with the dependecies
    /// being handled automatically based on the submission and execution order of the tasks
    ///
    /// One can also create sub buffers of sub buffers.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let mut a = vec![0;100];
    ///     let buf = mcl.register_buffer(mcl_rs::TaskArg::inout_slice(&mut c)
    ///            .resident(true)
    ///            .dynamic(true),
    ///     );
    ///     let sub_buf1 = buf.sub_buffer(10..20);
    ///     let sub_buf2 = buf.sub_buffer(15..25); // this call will succeed even though it overlaps with sub_buf1
    ///     let tasks = async move {
    ///         let pes: [u64; 3] = [1, 1, 1]
    ///         let task_1 = mcl.task("my_kernel", 1)
    ///                 .arg_buffer(mcl_rs::TaskArg::output_slice(sub_buf1))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes);
    ///         /// We can even create our next task sucessfully with the overlapping buffer because no actual work occurs until we call await
    ///         let task_2 = mcl.task("my_kernel", 1)
    ///                 .arg_buffer(mcl_rs::TaskArg::output_slice(sub_buf1))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes);
    ///     };
    ///     // drive both futures simultaneously -- based on the overlapping dependency, these task will in reality be executed serially
    ///     // as the internal implementation will prevent both tasks from allocating the overlapping sub_buffer regions simultaneously
    ///     futures::future::join_all([task_1,task_2]);
    ///     futures::executor::block_on(task);
    ///     
    ///```
    pub fn sub_buffer(&self, range: impl RangeBounds<usize>) -> Self {
        let u8_start = match range.start_bound() {
            Bound::Included(idx) => idx * self.meta.orig_type_size,
            Bound::Excluded(idx) => (idx - 1) * self.meta.orig_type_size,
            Bound::Unbounded => 0,
        };

        let u8_end = match range.end_bound() {
            Bound::Included(idx) => (idx + 1) * self.meta.orig_type_size,
            Bound::Excluded(idx) => idx * self.meta.orig_type_size,
            Bound::Unbounded => self.meta.len,
        };

        let len = u8_end - u8_start;
        let offset = self.meta.offset + u8_start;
        // println!("{len} {offset} {u8_start} {u8_end}");
        self.meta.cnt.fetch_add(1, Ordering::SeqCst);
        RegisteredBuffer {
            data: self.data.clone(),
            meta: BufferMetaData {
                orig_type_size: self.meta.orig_type_size,
                offset: offset,
                len: len,
                cnt: self.meta.cnt.clone(),
                my_alloc: Arc::new(Mutex::new(Alloc::new(self.data.data.len()))),
                parent_alloc: Some(self.meta.my_alloc.clone()),
                alloced: Arc::new(AtomicBool::new(false)),
            },
        }
    }

    async fn inner_alloc(&self, alloc: &Mutex<Alloc>) {
        while !self.meta.alloced.load(Ordering::SeqCst) {
            let mut alloc_guard = alloc.lock().unwrap();
            if let Ok(_) = alloc_guard.try_alloc(self.meta.offset, self.meta.offset + self.meta.len)
            {
                self.meta.alloced.store(true, Ordering::SeqCst);
            }
            drop(alloc_guard);
            async_std::task::yield_now().await;
        }
    }

    pub(crate) async fn alloc(&self) {
        if let Some(alloc) = self.meta.parent_alloc.as_ref() {
            //we are a sub array!()
            self.inner_alloc(alloc).await;
        } else {
            //we are not a sub array!
            self.inner_alloc(&self.meta.my_alloc).await;
        }
    }
}

/// Represents an MCL shared buffer, which is essentially a pointer
/// to data which exists in shared memory.
/// When only the `shared_mem` feature is turned on this buffer will exist in host shared memory only.
/// If instead the `pocl_extensions` feature is used, the the buffer will also exist in device shared memory.
/// Note that `pocl_extensions` requires a patched version of POCL 1.8 to have been succesfully
/// installed (please see <https://github.com/pnnl/mcl/tree/dev#using-custom-pocl-extensions> for more information).
///
/// A shared buffer allows tasks within different processes and applications to use the same buffer.
/// Further, we support creating sub-buffers of a shared buffers to alleviate some  of the overhead associated with
/// creating new buffers.
///
/// #Safety
/// Given that Shared Buffers can be used by multiple processes simultaenously they should
/// always be considered inherantly unsafe, as we are currently able to provide saftey gaurantees
/// within a single process. Please see the discussion on saftey for [RegisteredBuffer] for details
/// on the protections offered within a single process.
/// Given that Shared Buffers can be used by multiple tasks and processes simultaneously, and
/// that accelerators are often multi-threaded we try to ensure that RegisteredBuffers
/// are safe with respect to read and write access.
///
/// While we are unable to enforce read/write saftey guarantees across processes, the MCL library
/// does provide reference counting of the underlying shared memory buffer, and will release the
/// resources once all references across all proceesses have been dropped.
///
#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
pub struct SharedMemBuffer {
    addr: *mut c_void,
    size: usize,
    flags: ArgOpt,
    meta: BufferMetaData,
}

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
impl Clone for SharedMemBuffer {
    fn clone(&self) -> Self {
        self.meta.cnt.fetch_add(1, Ordering::SeqCst);
        SharedMemBuffer {
            addr: self.addr.clone(),
            size: self.size,
            flags: self.flags,
            meta: BufferMetaData {
                orig_type_size: self.meta.orig_type_size,
                offset: self.meta.offset,
                len: self.meta.len,
                cnt: self.meta.cnt.clone(),
                my_alloc: self.meta.my_alloc.clone(),
                parent_alloc: self.meta.parent_alloc.clone(),
                alloced: self.meta.alloced.clone(),
            },
        }
    }
}

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
impl Drop for SharedMemBuffer {
    fn drop(&mut self) {
        // println!("dropping {} {} {}",self.meta.offset,self.meta.len,self.meta.cnt.load(Ordering::SeqCst));
        if self.meta.cnt.fetch_sub(1, Ordering::SeqCst) == 1 {
            low_level::detach_shared_buffer(self.addr);
            if self.meta.alloced.load(Ordering::SeqCst) {
                if let Some(p_alloc) = &self.meta.parent_alloc {
                    //we are a sub array!
                    p_alloc
                        .lock()
                        .unwrap()
                        .free(self.meta.offset, self.meta.offset + self.meta.len);
                } else {
                    //we are not a sub array
                    self.meta
                        .my_alloc
                        .lock()
                        .unwrap()
                        .free(self.meta.offset, self.meta.offset + self.meta.len);
                }
                self.meta.alloced.store(false, Ordering::SeqCst);
            }
        }
    }
}

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions"))]
impl SharedMemBuffer {
    pub(crate) fn new(data: TaskArg<'_>) -> Self {
        let orig_type_size = data.orig_type_size;
        let (addr, len) = match &data.data {
            TaskArgData::Scalar(_) => panic!("cannot share a scalar"),
            TaskArgData::Buffer(_) => panic!("use the TaskArg::*_shared apis instead"),
            TaskArgData::Local(_) => panic!("cannot not share a local buffer"),
            TaskArgData::Shared(name, size) => {
                (low_level::get_shared_buffer(name, *size, data.flags), *size)
            }
            TaskArgData::Empty => panic!("cannot have an empty arg"),
        };

        // println!("SharedMemBuffer size {len}");

        SharedMemBuffer {
            addr: addr,
            size: len,
            flags: data.flags,
            meta: BufferMetaData {
                orig_type_size: orig_type_size,
                offset: 0,
                len: len,
                cnt: Arc::new(AtomicUsize::new(1)),
                my_alloc: Arc::new(Mutex::new(Alloc::new(len))),
                parent_alloc: None,
                alloced: Arc::new(AtomicBool::new(false)),
            },
        }
    }

    pub(crate) fn base_addr(&self) -> *mut c_void {
        self.addr
    }

    pub(crate) fn u8_offset(&self) -> i64 {
        self.meta.offset as i64
    }

    pub(crate) fn u8_len(&self) -> u64 {
        self.meta.len as u64
    }

    pub(crate) fn flags(&self) -> ArgOpt {
        self.flags
    }

    ///Return the offset into the original SharedMemBuffer this handle starts at.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let num_elems = 100;
    ///     let buf = mcl.create_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>("my_buffer", num_elems));
    ///     assert_eq!(buf.offset(),0);
    ///     let sub_buf = buf.sub_buffer(10..20);
    ///     assert_eq!(sub_buf.offset(),10);
    ///```
    pub fn offset(&self) -> usize {
        self.meta.offset / self.meta.orig_type_size
    }

    ///Return the len of this (sub)-SharedMemBuffer
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///     let num_elems = 100;
    ///     let buf = mcl.create_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>("my_buffer", num_elems));
    ///     assert_eq!(buf.len(),100);
    ///     let sub_buf = buf.sub_buffer(10..20);
    ///     assert_eq!(sub_buf.len(),10);
    ///```
    pub fn len(&self) -> usize {
        self.meta.len / self.meta.orig_type_size
    }

    /// Try to manually detach from this shared memory segment (i.e. decrement the global buffer reference count), this will only succeed if this is the last reference locally
    ///
    /// NOTE: Dropping a handle potentially calls this automatically provided it is the last local (to this process) reference to the buffer.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///
    ///     let num_elems = 100;
    ///     let buf = mcl.create_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>("my_buffer", num_elems));
    ///     let sub_buf = buf.sub_buffer(10..20);
    ///
    ///     let buf = buf.try_detach();
    ///     assert!(buf.is_err());
    ///     let buf = buf.unwrap_err();
    ///     drop(sub_buf);
    ///     assert!(buf.try_detach().is_ok())
    ///```
    pub fn try_detach(self) -> Result<(), Self> {
        if self.meta.cnt.fetch_sub(1, Ordering::SeqCst) == 1 {
            low_level::detach_shared_buffer(self.addr);
            Ok(())
        } else {
            Err(self)
        }
    }

    /// Creates a sub buffer using the provided range from a given SharedMemBuffer
    /// The sub buffer essentially "locks" the elements in the provided range
    /// *BUT ONLY ON THE CALLING PROCESS* (other processes will have no idea of these locked regions)
    /// delaying other (local to this process) sub buffers from executing with overlapping elements until all references to this sub buffer
    /// have been dropped.
    /// Note that sub buffer element locking happens at task execution time rather that sub buffer handle creation.
    /// This allows overlapping sub buffers be created and passed as arguments to different tasks, with the dependecies
    /// being handled automatically based on the submission and execution order of the tasks
    ///
    /// One can also create sub buffers of sub buffers.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///
    ///     let num_elems = 100;
    ///     let buf = mcl.create_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>("my_buffer", num_elems));
    ///     let sub_buf1 = buf.sub_buffer(10..20);
    ///     let sub_buf2 = buf.sub_buffer(15..25); // this call will succeed even though it overlaps with sub_buf1
    ///     let tasks = async move {
    ///         let pes: [u64; 3] = [1, 1, 1]
    ///         let task_1 = mcl.task("my_kernel", 1)
    ///                 .arg_buffer(mcl_rs::TaskArg::output_slice(sub_buf1))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes);
    ///         let task_1 = mcl.task("my_kernel", 1)
    ///                 .arg_buffer(mcl_rs::TaskArg::output_slice(sub_buf1))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes);
    ///         /// We can even create our next task sucessfully with the overlapping buffer because no actual work occurs until we call await
    ///         let task_2 = mcl.task("my_kernel", 1)
    ///                 .arg_buffer(mcl_rs::TaskArg::output_slice(sub_buf1))
    ///                 .dev(mcl_rs::DevType::CPU)
    ///                 .exec(pes);
    ///     }
    ///     // drive both futures simultaneously -- based on the overlapping dependency, these task will in reality be executed serially
    ///     // as the internal implementation will prevent both tasks from allocating the overlapping sub_buffer regions simultaneously
    ///     futures::future::join_all([task_1,task_2]);
    ///     futures::executor::block_on(task);
    ///     
    ///```
    pub fn sub_buffer(&self, range: impl RangeBounds<usize>) -> Self {
        let u8_start = match range.start_bound() {
            Bound::Included(idx) => idx * self.meta.orig_type_size,
            Bound::Excluded(idx) => (idx - 1) * self.meta.orig_type_size,
            Bound::Unbounded => 0,
        };

        let u8_end = match range.end_bound() {
            Bound::Included(idx) => (idx + 1) * self.meta.orig_type_size,
            Bound::Excluded(idx) => idx * self.meta.orig_type_size,
            Bound::Unbounded => self.meta.len,
        };

        let len = u8_end - u8_start;
        let offset = self.meta.offset + u8_start;
        // println!("{len} {offset} {u8_start} {u8_end}");
        self.meta.cnt.fetch_add(1, Ordering::SeqCst);
        SharedMemBuffer {
            addr: self.addr.clone(),
            size: self.size,
            flags: self.flags.clone(),
            meta: BufferMetaData {
                orig_type_size: self.meta.orig_type_size,
                offset: offset,
                len: len,
                cnt: self.meta.cnt.clone(),
                my_alloc: Arc::new(Mutex::new(Alloc::new(self.size))),
                parent_alloc: Some(self.meta.my_alloc.clone()),
                alloced: Arc::new(AtomicBool::new(false)),
            },
        }
    }

    /// Extract a T slice from this SharedMemBuffer handle.
    ///
    /// #Saftey
    /// This is unsafe as we currently have no mechanism to guarantee the alignment of T with the alignment used to originally create the buffer
    /// potentially in a different process. The user must ensure the alignment is valid otherwise behavior is undefined.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///
    ///     let num_elems = 100;
    ///     let buf = mcl.attach_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>("my_buffer", num_elems));
    ///     let sliced = unsafe { buf.as_slice::<u32>()};
    ///```
    pub unsafe fn as_slice<T>(&self) -> &[T] {
        assert_eq!(self.meta.len % std::mem::size_of::<T>(),0, "Leftover bytes when tryin to create slice i.e. (buffer len in bytes) % (size of T) != 0");
        std::slice::from_raw_parts(
            self.addr as *const T,
            self.meta.len / std::mem::size_of::<T>(),
        )
    }

    /// Extract a T slice from this SharedMemBuffer handle.
    ///
    /// #Saftey
    /// This is unsafe as we currently have no mechanism to guarantee the alignment of T with the alignment used to originally create the buffer
    /// potentially in a different process. The user must ensure the alignment is valid otherwise behavior is undefined.
    ///
    /// # Examples
    ///```no_run
    ///     let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();
    ///     mcl.load_prog("my_path", mcl_rs::PrgType::Src);
    ///
    ///     let num_elems = 100;
    ///     let buf = mcl.attach_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>("my_buffer", num_elems));
    ///     let slice = unsafe { buf.as_mut_slice::<u32>()};
    ///```
    pub unsafe fn as_mut_slice<T>(&self) -> &mut [T] {
        assert_eq!(self.meta.len % std::mem::size_of::<T>(),0, "Leftover bytes when tryin to create slice i.e. (buffer len in bytes) % (size of T) != 0");
        std::slice::from_raw_parts_mut(self.addr as *mut T, self.meta.len)
    }

    async fn inner_alloc(&self, alloc: &Mutex<Alloc>) {
        while !self.meta.alloced.load(Ordering::SeqCst) {
            let mut alloc_guard = alloc.lock().unwrap();
            if let Ok(_) = alloc_guard.try_alloc(self.meta.offset, self.meta.offset + self.meta.len)
            {
                self.meta.alloced.store(true, Ordering::SeqCst);
            }
            drop(alloc_guard);
            async_std::task::yield_now().await;
        }
    }

    pub(crate) async fn alloc(&self) {
        if let Some(alloc) = self.meta.parent_alloc.as_ref() {
            //we are a sub array!()
            self.inner_alloc(alloc).await;
        } else {
            //we are not a sub array!
            self.inner_alloc(&self.meta.my_alloc).await;
        }
    }
}
