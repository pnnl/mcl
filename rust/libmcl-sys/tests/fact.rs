use libmcl_sys::*;
use rand::Rng;
use std::convert::TryInto;
use std::ffi::{c_void, CString};
use std::mem::size_of;

fn fact_seq(v_in: &Vec<u64>, v_out: &mut Vec<u64>) {
    for i in 0..v_in.len() {
        let mut f = 1;

        for j in 1..=v_in[i] {
            f = f * j;
        }
        v_out[i] = f;
    }
}

fn fact_mcl(v_in: &mut Vec<u64>, v_out: &mut Vec<u64>, reps: usize, sync: &bool) {
    let mut hdls: Vec<*mut mcl_handle> = Vec::new();
    unsafe {
        for i in 0..reps {
            let mut pes: [u64; 3] = [1, 1, 1];
            let mut les: [u64; 3] = [1; 3];
            let kernel_path = CString::new("tests/fact.cl").unwrap();
            let kernel_name = CString::new("FACT").unwrap();

            let empty = CString::new("").unwrap();
            mcl_prg_load(kernel_path.into_raw(), empty.into_raw(), MCL_PRG_SRC.into());

            hdls.push(mcl_task_create());

            // Get a raw void ptr to our data to pass it to mcl C inteface
            let v_in_ptr: *mut c_void = v_in.as_mut_ptr() as *mut c_void;
            let v_out_ptr: *mut c_void = v_out.as_mut_ptr() as *mut c_void;
            let pes_ptr: *mut u64 = &mut pes as *mut _ as *mut u64;
            let les_ptr: *mut u64 = &mut les as *mut _ as *mut u64;

            assert_eq!(mcl_task_set_kernel(hdls[i], kernel_name.into_raw(), 2), 0);
            assert_eq!(
                mcl_task_set_arg(
                    hdls[i],
                    0,
                    v_in_ptr.offset((i * size_of::<u64>()).try_into().unwrap()),
                    size_of::<u64>() as u64,
                    (MCL_ARG_BUFFER | MCL_ARG_INPUT).into()
                ),
                0
            );
            assert_eq!(
                mcl_task_set_arg(
                    hdls[i],
                    1,
                    v_out_ptr.offset((i * size_of::<u64>()).try_into().unwrap()),
                    size_of::<u64>() as u64,
                    (MCL_ARG_BUFFER | MCL_ARG_OUTPUT).into()
                ),
                0
            );
            assert_eq!(mcl_exec(hdls[i], pes_ptr, les_ptr, MCL_TASK_GPU.into()), 0);

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
fn fact() {
    unsafe {
        let workers = 2;
        let reps = 40;
        let vec_size = reps;
        let sync = true;

        assert_eq!(mcl_init(workers, MCL_NULL.into()), 0);

        let mut rng = rand::thread_rng();

        // Generate v_in, v_out and v_out_seq arrays of size vec_size and initialize with 0
        let mut v_in: Vec<u64> = (0..vec_size).map(|_| rng.gen_range(1..10)).collect();
        let mut v_out: Vec<u64> = vec![0; vec_size];
        let mut v_out_seq: Vec<u64> = vec![0; vec_size];

        fact_seq(&v_in, &mut v_out_seq);

        println!("Sync mcl fact");
        fact_mcl(&mut v_in, &mut v_out, reps, &sync);
        assert_eq!(v_out_seq, v_out);

        let mut v_out: Vec<u64> = vec![0; vec_size];
        let sync = false;

        println!("Async mcl fact");
        fact_mcl(&mut v_in, &mut v_out, reps, &sync);
        assert_eq!(v_out_seq, v_out);

        assert_eq!(mcl_finit(), 0);
    }
}
