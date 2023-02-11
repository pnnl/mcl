use crate::low_level;
use crate::task::{TaskArg,TaskArgData};
use crate::ArgOpt;

use std::sync::{Arc,Mutex};
use std::sync::atomic::{AtomicUsize,AtomicBool,Ordering};
use std::ops::RangeBounds;
use std::collections::{Bound,BTreeMap};
use std::ffi::c_void;

struct Alloc {
    size: usize,
    currently_allocated: BTreeMap<usize,usize>,
}

impl Alloc {
    fn new(size: usize) -> Self {
        Alloc{
            size: size,
            currently_allocated: BTreeMap::new(),
        }
    }

    fn try_alloc(&mut self, start_i: usize, end_i: usize) -> Result<(),()> {
        if self.currently_allocated.is_empty()  { //no other allocations yet
            self.currently_allocated.insert(start_i,end_i);
            println!("{:?}",self.currently_allocated);
            return Ok(())
        }
        else { //other allocations exist
            if self.currently_allocated.contains_key(&start_i) { //something already allocated from same index
                Err(())
            }
            else { //no other allocations use the same start index, but I may exist with another allocation 
                let mut iter = self.currently_allocated.range(start_i..end_i);
                if let Some(next_lowest) = iter.clone().rev().next(){// check the closest allocation that starts below me, or there are no allocations lower than me
                    if start_i < *next_lowest.1 { // i start before this allocation ends
                        return Err(())
                    }
                }
                //at this point we know start_i doesnt exist in a previous allocation now check end_i
                if let Some(_) = iter.next() { //we know some other allocation exists between start_i and end_i
                    return Err(())
                }
                //woohoo we can allocate!
                self.currently_allocated.insert(start_i,end_i);
                println!("{:?}",self.currently_allocated);
                return Ok(())
            }
        }
    }

    fn free(&mut self, start_i: usize, end_i: usize) {
        if let Some(v) = self.currently_allocated.remove(&start_i){
            assert_eq!(v,end_i,"unexepected subbuffer end index: {start_i} {v} {end_i}")
        }
        else {
            println!("{:?}",self.currently_allocated);
            panic!("unexepected subbuffer start index: {start_i}")
        }
    }
}

pub struct RegisteredBuffer<'a> {
    data: TaskArg<'a>,
    orig_type_size: usize,
    offset: usize,
    len: usize,
    cnt: Arc<AtomicUsize>,
    my_alloc: Arc<Mutex<Alloc>>,
    parent_alloc: Option<Arc<Mutex<Alloc>>>,
    alloced: Arc<AtomicBool>
}



impl<'a> Clone for RegisteredBuffer<'a> {
    fn clone(&self) -> Self {
        self.cnt.fetch_add(1,Ordering::SeqCst);
        RegisteredBuffer{
            data: self.data.clone(),
            orig_type_size: self.orig_type_size,
            offset: self.offset,
            len: self.len,
            cnt: self.cnt.clone(),
            my_alloc: self.my_alloc.clone(),
            parent_alloc: self.parent_alloc.clone(),
            alloced: self.alloced.clone(),
        }
    }
}


impl <'a> Drop for RegisteredBuffer<'a> {
    fn drop(&mut self) {
        println!("dropping {} {} {}",self.offset,self.len,self.cnt.load(Ordering::SeqCst));
        if self.cnt.fetch_sub(1,Ordering::SeqCst) == 1 {
            match &self.data.data {
                TaskArgData::Buffer(x) => low_level::unregister_buffer( x),
                _ => unreachable!("Can not have a Registered Buffer that is not a buffer"),
            }
            if self.alloced.load(Ordering::SeqCst){ 
                if let Some(p_alloc) = &self.parent_alloc { //we are a sub array!
                    p_alloc.lock().unwrap().free(self.offset,self.offset+self.len);
                }
                else { //we are not a sub array
                    self.my_alloc.lock().unwrap().free(self.offset,self.offset+self.len);
                }
                self.alloced.store(false,Ordering::SeqCst);
            }
        }
        
    }
}

impl <'a> RegisteredBuffer<'a> {
    pub(crate) fn new(data: TaskArg<'a>) -> Self {
        let orig_type_size = data.orig_type_size;
        let len = data.data.len();
        println!("dots: {orig_type_size} len: {len}");
        match &data.data {
            TaskArgData::Scalar(_) => panic!("cannot register a scalar"),
            TaskArgData::Buffer(x) => low_level::register_buffer( x, data.flags),
            TaskArgData::Local(_) => panic!("Must register a buffer"),
            TaskArgData::Empty => panic!("cannot have an empty arg"),
        }
        RegisteredBuffer{
            data: data,
            orig_type_size: orig_type_size,
            offset: 0,
            len: len,
            cnt: Arc::new(AtomicUsize::new(1)),
            my_alloc: Arc::new(Mutex::new(Alloc::new(len))),
            parent_alloc: None,
            alloced: Arc::new(AtomicBool::new(false)),
        }
    }

    pub(crate) fn base_addr(&self) -> *mut c_void{
        match &self.data.data {
            TaskArgData::Buffer(x) => x.as_ptr() as *mut c_void,
            _ => unreachable!("Can not have a Registered Buffer that is not a buffer"),
        }
    }

    pub(crate) fn u8_offset(&self) -> i64 {
        self.offset as i64
    }

    pub(crate) fn u8_len(&self) -> u64 {
        self.len as u64
    }

    pub(crate) fn flags(&self) -> ArgOpt {
        self.data.flags
    }

    pub fn offset(&self) -> usize {
        self.offset/self.orig_type_size
    }

    pub fn len(&self) -> usize {
        self.len/self.orig_type_size
    }

    pub fn try_unregister(self) -> Result<(),Self>{
        if let Ok(_) = self.cnt.compare_exchange(1,0,Ordering::SeqCst,Ordering::SeqCst) {
            match &self.data.data {
                TaskArgData::Buffer(x) => low_level::unregister_buffer( x),
                _ => unreachable!("Can not have a Registered Buffer that is not a buffer"),
            }
            Ok(())
        }
        else{
            Err(self)
        }
    }

    pub fn try_invalidate(&self) -> bool{
        if self.cnt.fetch_sub(1,Ordering::SeqCst) == 1 {
            match &self.data.data {
                TaskArgData::Buffer(x) => low_level::invalidate_buffer( x),
                _ => unreachable!("Can not have a Registered Buffer that is not a buffer"),
            }
            true
        }
        else{
            false
        }
    }

    pub fn sub_buffer(&self, range: impl RangeBounds<usize>) -> Self {
        let u8_start = match range.start_bound() {
            Bound::Included(idx) => idx*self.orig_type_size,
            Bound::Excluded(idx) =>  (idx-1)*self.orig_type_size,
            Bound::Unbounded => 0
        };

        let u8_end = match range.end_bound() {
            Bound::Included(idx) => (idx+1)*self.orig_type_size,
            Bound::Excluded(idx) =>  idx*self.orig_type_size,
            Bound::Unbounded => self.len
        };

        let len = u8_end - u8_start;
        let offset = self.offset + u8_start;
        println!("{len} {offset} {u8_start} {u8_end}");
        self.cnt.fetch_add(1,Ordering::SeqCst);
        RegisteredBuffer{
            data: self.data.clone(),
            orig_type_size: self.orig_type_size,
            offset: offset,
            len: len,
            cnt: self.cnt.clone(),
            my_alloc: Arc::new(Mutex::new(Alloc::new(self.data.data.len()))),
            parent_alloc: Some(self.my_alloc.clone()),
            alloced: Arc::new(AtomicBool::new(false)),
        }
    }

    async fn inner_alloc(&self, alloc: &Mutex<Alloc>) {
        while !self.alloced.load(Ordering::SeqCst) {
            let mut alloc_guard = alloc.lock().unwrap();
            if let Ok(_) = alloc_guard.try_alloc(self.offset,self.offset+self.len){
                self.alloced.store(true,Ordering::SeqCst);
            }
            drop(alloc_guard);
            async_std::task::yield_now().await;
        }
        
    }

    

    pub(crate) async fn alloc(&self) {
        if let Some(alloc) = self.parent_alloc.as_ref() { //we are a sub array!()
            self.inner_alloc(alloc).await;
        }
        else { //we are not a sub array!
            self.inner_alloc(&self.my_alloc).await;
        }
    }
}