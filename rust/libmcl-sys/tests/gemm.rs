use libmcl_sys::*;
use std::ffi::{CString, c_void};
use std::mem::size_of;
use rand::Rng;

fn gemm_seq(a: &Vec::<i32>, b: &Vec::<i32>, c: &mut Vec::<i32>, n: usize) {

    for i in 0..n {
        for j in 0..n {
            for k in 0.. n {
                c[i * n + j] += a[i * n + k] * b[k * n + j] 
            }
        }
    }
}

fn gemm_mcl(a: &mut Vec::<i32>, b: &mut Vec::<i32>, c: &mut Vec::<i32>, n: &mut usize, reps: usize, sync: &bool) {

    let mut hdls : Vec::<*mut mcl_handle> = Vec::new();
    unsafe {

        for i in 0..reps {

            let size : u64 = c.len() as u64;
            let mut pes: [u64; 3] = [*n as u64, *n as u64, 1];
            let mut les: [u64; 3] = [1; 3];
            let kernel_path = CString::new("tests/gemmN.cl").unwrap();
            let kernel_name = CString::new("gemmN").unwrap();
            
            let empty = CString::new("").unwrap();
            mcl_prg_load( kernel_path.into_raw(), empty.into_raw(),MCL_PRG_SRC.into());
            hdls.push(mcl_task_create());

            // Get a raw void ptr to our data to pass it to mcl C inteface
            let a_ptr : *mut c_void = a.as_mut_ptr()  as *mut c_void;
            let b_ptr : *mut c_void = b.as_mut_ptr()  as *mut c_void;
            let c_ptr : *mut c_void = c.as_mut_ptr()  as *mut c_void;
            let n_ptr : *mut c_void = n as *mut _ as *mut c_void;
            let pes_ptr: *mut u64 = &mut pes as *mut _ as *mut u64;
            let les_ptr: *mut u64 = &mut les as *mut _ as *mut u64;
            
            assert_eq!(mcl_task_set_kernel(hdls[i], kernel_name.into_raw(), 4),0);
            assert_eq!(mcl_task_set_arg(hdls[i], 0, a_ptr, size * size_of::<i32>() as u64, (MCL_ARG_BUFFER| MCL_ARG_INPUT).into()), 0);
            assert_eq!(mcl_task_set_arg(hdls[i], 1, b_ptr, size * size_of::<i32>() as u64, (MCL_ARG_BUFFER| MCL_ARG_INPUT).into()), 0);
            assert_eq!(mcl_task_set_arg(hdls[i], 2, n_ptr, size_of::<usize>() as u64, (MCL_ARG_SCALAR| MCL_ARG_INPUT).into()), 0);
            assert_eq!(mcl_task_set_arg(hdls[i], 3, c_ptr, size * size_of::<i32>() as u64, (MCL_ARG_BUFFER| MCL_ARG_OUTPUT).into()), 0);
            assert_eq!(mcl_exec(hdls[i], pes_ptr, les_ptr, MCL_TASK_GPU.into()),0);
            
            if *sync {
                assert_eq!(mcl_wait(hdls[i]), 0);
            }
        }

        if !*sync {
            for i in 0..reps {
                assert_eq!(mcl_wait(hdls[i]), 0);
            }
        }

        for i in 0..reps {
            assert_eq!(mcl_hdl_free(hdls[i]), 0);
        }
    }
    
}

#[test]
fn gemm() {
    unsafe {
        
        let workers = 2;
        let mut n  = 128;
        let nn = n * n;
        let reps = 100;
        let sync = false;
        
        assert_eq!(mcl_init(workers, MCL_NULL.into()), 0);

        
        let mut rng = rand::thread_rng();

        // Generate a and b matrices of size NxN and initialize with random numbers in [0, 100)
        let mut a: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();
        let mut b: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();

        // Allocate the c matrix that will hold the results.
        let mut c: Vec::<i32> = vec![0; nn];
        let mut c_seq: Vec::<i32> = vec![0; nn];

        gemm_seq(&a, &b, &mut c_seq, n);

        println!("Async mcl gemm");
        gemm_mcl(&mut a, &mut b, &mut c, &mut n, reps, &sync);
        assert_eq!(c_seq, c);

        let mut c = vec![0; nn];
        let sync = true;



        println!("Sync mcl gemm");
        gemm_mcl(&mut a, &mut b, &mut c, &mut n, reps, &sync);
        assert_eq!(c_seq, c);


        assert_eq!(mcl_finit(), 0);
    }
}