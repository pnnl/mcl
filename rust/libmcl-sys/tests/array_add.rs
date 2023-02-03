use libmcl_sys::*;
use std::ffi::{CString, c_void};
use std::mem::size_of;
use rand::Rng;

fn add_seq(x: &Vec::<i32>, y: &Vec::<i32>, z: &mut Vec::<i32>) {

    for i in 0..z.len(){
        z[i] = x[i] + y[i];
    }
}

fn add_mcl(x: &mut Vec::<i32>, y: &mut Vec::<i32>, z: &mut Vec::<i32>, reps: usize, sync: &bool) {

    let mut hdls : Vec::<*mut mcl_handle> = Vec::new();
    unsafe {

        for i in 0..reps {

            let size : u64 = z.len() as u64;
            let mut pes: [u64; 3] = [size, 1, 1];
            let mut les: [u64; 3] = [1; 3];
            let kernel_path = CString::new("tests/vadd.cl").unwrap();
            let kernel_name = CString::new("VADD").unwrap();
            
            let empty = CString::new("").unwrap();
            mcl_prg_load( kernel_path.into_raw(), empty.into_raw(),MCL_PRG_SRC.into());
            hdls.push(mcl_task_create());

            // Get a raw void ptr to our data to pass it to mcl C inteface

            let x_ptr : *mut c_void = x.as_mut_ptr()  as *mut c_void;
            let y_ptr : *mut c_void = y.as_mut_ptr()  as *mut c_void;
            let z_ptr : *mut c_void = z.as_mut_ptr()  as *mut c_void;
            let pes_ptr: *mut u64 = &mut pes as *mut _ as *mut u64;
            let les_ptr: *mut u64 = &mut les as *mut _ as *mut u64;
            
            assert_eq!(mcl_task_set_kernel(hdls[i], kernel_name.into_raw(), 3),0);
            assert_eq!(mcl_task_set_arg(hdls[i], 0, x_ptr, size * size_of::<i32>() as u64, (MCL_ARG_BUFFER| MCL_ARG_INPUT).into()), 0);
            assert_eq!(mcl_task_set_arg(hdls[i], 1, y_ptr, size * size_of::<i32>() as u64, (MCL_ARG_BUFFER| MCL_ARG_INPUT).into()), 0);
            assert_eq!(mcl_task_set_arg(hdls[i], 2, z_ptr, size * size_of::<i32>() as u64, (MCL_ARG_BUFFER| MCL_ARG_OUTPUT).into()), 0);
            assert_eq!(mcl_exec(hdls[i], pes_ptr, les_ptr, MCL_TASK_GPU.into()),0);
            
            if *sync {
                assert_eq!(mcl_wait(hdls[i]), 0);
            }
        }

        if !*sync {
            assert_eq!(mcl_wait_all(), 0);
        }

        for i in 0..reps {
            assert_eq!(mcl_hdl_free(hdls[i]), 0);
        }
    }
    
}

#[test]
fn vadd() {
    unsafe {
        
        let workers = 2;
        let vec_size = 128;
        let reps = 100;
        let sync = false;
        
        assert_eq!(mcl_init(workers, MCL_NULL.into()), 0);

        
        let mut rng = rand::thread_rng();

        // Generate x and y arrays of size vec_size and initialize with random numbers in [0, 100)
        let mut x: Vec::<i32> = (0..vec_size).map(|_| {rng.gen_range(0..100)}).collect();
        let mut y: Vec::<i32> = (0..vec_size).map(|_| {rng.gen_range(0..100)}).collect();

        // Allocate the z vectors that will hold the results.
        let mut z: Vec::<i32> = vec![0; vec_size];
        let mut z_seq: Vec::<i32> = vec![0; vec_size];

        add_seq(&x, &y, &mut z_seq);

        println!("Async mcl add");
        add_mcl(&mut x, &mut y, &mut z, reps, &sync);
        assert_eq!(z_seq, z);

        let mut z = vec![0; vec_size];
        let sync = true;



        println!("Sync mcl add");
        add_mcl(&mut x, &mut y, &mut z, reps, &sync);
        assert_eq!(z_seq, z);


        assert_eq!(mcl_finit(), 0);
    }
}