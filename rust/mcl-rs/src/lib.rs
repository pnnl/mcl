//! # mcl-rs
//! This project hosts the high-level wrappers of the mcl rust bindings.
//! 
//! ## Summary
//! This crate provides high-level, rust-friendly bindings for mcl. The purpose of these bindings are
//! to expose a user-friendlier API to what the low-level libmcl-sys API offers. It provides wrappers
//! for all mcl public functions and tries to provide safety at compilation type, however,
//! because of the nature of the library counting on a C project there are cases that it's only possible
//! to catch errors at runtime.


mod low_level;
mod device;
pub use device::*;
mod prog;
pub use prog::*;
mod mcl;
pub use mcl::*;
mod task;
pub use task::*;
mod transfer;
pub use transfer::*;

use paste::paste;
use std::ffi::c_void;

#[allow(non_camel_case_types)]
#[derive(Clone)]
pub enum MclScalarDataType<'a> {
    u8(&'a u8),
    u16(&'a u16),
    u32(&'a u32),
    u64(&'a u64),
    usize(&'a usize),
    i8(&'a i8),
    i16(&'a i16),
    i32(&'a i32),
    i64(&'a i64),
    isize(&'a isize),
    f32(&'a f32),
    f64(&'a f64),
}

#[allow(non_camel_case_types)]
#[derive(Clone)]
pub struct MclMutScalarDataType<'a> {
    inner: MclBufferDataType<'a>
}

#[allow(non_camel_case_types)]
#[derive(Clone)]
pub enum MclBufferDataType<'a> {
    u8(&'a [u8]),
    u16(&'a [u16]),
    u32(&'a [u32]),
    u64(&'a [u64]),
    usize(&'a [usize]),
    i8(&'a [i8]),
    i16(&'a [i16]),
    i32(&'a [i32]),
    i64(&'a [i64]),
    isize(&'a [isize]),
    f32(&'a [f32]),
    f64(&'a [f64]),
}

#[allow(non_camel_case_types)]
#[derive(Clone)]
pub struct MclMutBufferDataType<'a> {
    inner: MclBufferDataType<'a>
}

impl<'a> From<MclScalarDataType<'a>> for MclBufferDataType<'a> {
    fn from(data: MclScalarDataType<'a>) -> MclBufferDataType<'a>{
        match data {
            MclScalarDataType::u8(x) => MclBufferDataType::u8(std::slice::from_ref(x)),
            MclScalarDataType::u16(x) => MclBufferDataType::u16(std::slice::from_ref(x)),
            MclScalarDataType::u32(x) => MclBufferDataType::u32(std::slice::from_ref(x)),
            MclScalarDataType::u64(x) => MclBufferDataType::u64(std::slice::from_ref(x)),
            MclScalarDataType::usize(x) => MclBufferDataType::usize(std::slice::from_ref(x)),
            MclScalarDataType::i8(x) => MclBufferDataType::i8(std::slice::from_ref(x)),
            MclScalarDataType::i16(x) => MclBufferDataType::i16(std::slice::from_ref(x)),
            MclScalarDataType::i32(x) => MclBufferDataType::i32(std::slice::from_ref(x)),
            MclScalarDataType::i64(x) => MclBufferDataType::i64(std::slice::from_ref(x)),
            MclScalarDataType::isize(x) => MclBufferDataType::isize(std::slice::from_ref(x)),
            MclScalarDataType::f32(x) => MclBufferDataType::f32(std::slice::from_ref(x)),
            MclScalarDataType::f64(x) => MclBufferDataType::f64(std::slice::from_ref(x)),
        }
    }
}



macro_rules! impl_mcl_scalar_data_type{
    () => {};
    ($t:ty $(, $more:ty)*) => {
        paste! {
            // impl<'a> From<$t> for MclScalarDataType<'a> {
            //     fn from(value: $t) -> Self {
            //         MclScalarDataType::$t(&value)
            //     }
            // }
            // impl<'a> From<$t> for MclMutScalarDataType<'a> {
            //     fn from(value: $t) -> Self {
            //         MclMutScalarDataType{inner: MclScalarDataType::$t(&value)}
            //     }
            // }
            impl<'a> From<&'a $t> for MclScalarDataType<'a> {
                fn from(value: &'a $t) -> Self {
                    MclScalarDataType::$t(value)
                }
            }
            impl<'a> From<&'a mut $t> for MclMutScalarDataType<'a> {
                fn from(value:  &'a mut $t) -> Self {
                    MclMutScalarDataType{inner: MclBufferDataType::$t(std::slice::from_ref(value))}
                }
            }
            impl<'a> From<&'a[$t]> for MclBufferDataType<'a> {
                fn from(value: &'a [$t]) -> Self {
                    MclBufferDataType::$t(value)
                }
            }
            impl<'a> From<&'a mut [$t]> for MclMutBufferDataType<'a> {
                fn from(value: &'a mut [$t]) -> Self {
                    MclMutBufferDataType{inner: MclBufferDataType::$t(value)}
                }
            }
            impl<'a> From<&'a Vec<$t>> for MclBufferDataType<'a> {
                fn from(value: &'a Vec<$t>) -> Self {
                    MclBufferDataType::$t(value)
                }
            }
            impl<'a> From<&'a mut Vec<$t>> for MclMutBufferDataType<'a> {
                fn from(value: &'a mut Vec<$t>) -> Self {
                    MclMutBufferDataType{inner: MclBufferDataType::$t(value)}
                }
            }
            impl_mcl_scalar_data_type!($($more),*);
        }
    };
}



impl_mcl_scalar_data_type!(u8,u16,u32,u64,usize,i8,i16,i32,i64,isize,f32,f64);

pub(crate) trait MclData {
    fn as_c_void(&self) -> *mut c_void;
    fn size(&self) -> u64;
}

impl<'a> MclData for &MclScalarDataType<'a>{
    fn as_c_void(&self) -> *mut c_void {
        match self {
            MclScalarDataType::u8(x) => (*x) as *const u8 as  *mut c_void,
            MclScalarDataType::u16(x) => (*x) as *const u16 as *mut c_void,
            MclScalarDataType::u32(x) => (*x) as *const u32 as *mut c_void,
            MclScalarDataType::u64(x) => (*x) as *const u64 as *mut c_void,
            MclScalarDataType::usize(x) => (*x) as *const usize as *mut c_void,
            MclScalarDataType::i8(x) => (*x) as *const i8 as *mut c_void,
            MclScalarDataType::i16(x) => (*x) as *const i16 as *mut c_void,
            MclScalarDataType::i32(x) => (*x) as *const i32 as *mut c_void,
            MclScalarDataType::i64(x) => (*x) as *const i64 as *mut c_void,
            MclScalarDataType::isize(x) => (*x) as *const isize as *mut c_void,
            MclScalarDataType::f32(x) => (*x) as *const f32 as *mut c_void,
            MclScalarDataType::f64(x) => (*x) as *const f64 as *mut c_void,
        }
    }
    fn size(&self) -> u64 {
        (match self {
            MclScalarDataType::u8(_) => std::mem::size_of::<u8>() ,
            MclScalarDataType::u16(_) => std::mem::size_of::<u16>(),
            MclScalarDataType::u32(_) => std::mem::size_of::<u32>(),
            MclScalarDataType::u64(_) => std::mem::size_of::<u64>(),
            MclScalarDataType::usize(_) => std::mem::size_of::<usize>(),
            MclScalarDataType::i8(_) => std::mem::size_of::<i8>(),
            MclScalarDataType::i16(_) => std::mem::size_of::<i16>(),
            MclScalarDataType::i32(_) => std::mem::size_of::<i32>(),
            MclScalarDataType::i64(_) => std::mem::size_of::<i64>(),
            MclScalarDataType::isize(_) => std::mem::size_of::<isize>(),
            MclScalarDataType::f32(_) => std::mem::size_of::<f32>(),
            MclScalarDataType::f64(_) => std::mem::size_of::<f64>(),
        }) as u64
    }
}

impl<'a> MclData for &MclBufferDataType<'a>{
    fn as_c_void(&self) -> *mut c_void {
        match self {
            MclBufferDataType::u8(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::u16(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::u32(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::u64(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::usize(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::i8(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::i16(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::i32(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::i64(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::isize(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::f32(x) => x.as_ptr() as *mut c_void,
            MclBufferDataType::f64(x) => x.as_ptr() as *mut c_void,
        }
    }
    fn size(&self) -> u64 {
        (match self {
            MclBufferDataType::u8(x) => x.len()*std::mem::size_of::<u8>() ,
            MclBufferDataType::u16(x) => x.len()*std::mem::size_of::<u16>(),
            MclBufferDataType::u32(x) => x.len()*std::mem::size_of::<u32>(),
            MclBufferDataType::u64(x) => x.len()*std::mem::size_of::<u64>(),
            MclBufferDataType::usize(x) => x.len()*std::mem::size_of::<usize>(),
            MclBufferDataType::i8(x) => x.len()*std::mem::size_of::<i8>(),
            MclBufferDataType::i16(x) => x.len()*std::mem::size_of::<i16>(),
            MclBufferDataType::i32(x) => x.len()*std::mem::size_of::<i32>(),
            MclBufferDataType::i64(x) => x.len()*std::mem::size_of::<i64>(),
            MclBufferDataType::isize(x) => x.len()*std::mem::size_of::<isize>(),
            MclBufferDataType::f32(x) => x.len()*std::mem::size_of::<f32>(),
            MclBufferDataType::f64(x) => x.len()*std::mem::size_of::<f64>(),
        }) as u64
    }
}
//probably give up on the macro in the enum but we can do the impl part i think
// pub(crate) enum ScalarType{
//     u8(u8),
//     U16(u16),
//     U32(u32),
//     U64(u64),
//     Usize(usize),
//     I8(i8),
//     I16(i16),
//     I32(i32),
//     I64(i64),
//     Isize(isize),
//     F32(f32),
//     F64(f64),
// }

// pub(crate) BufferType{
//     U8(u8),
//     U16(u16),
//     U32(u32),
//     U64(u64),
//     Usize(usize),
//     I8(i8),
//     I16(i16),
//     I32(i32),
//     I64(i64),
//     Isize(isize),
//     F32(f32),
//     F64(f64),
// }