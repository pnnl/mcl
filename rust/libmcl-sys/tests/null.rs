use libmcl_sys::*;

#[test]
fn null_hdl() {
    unsafe {
        let nreqs: usize = 10;
        let mut ret = 0;
        let mut hdls: Vec<*mut mcl_handle> = Vec::<*mut mcl_handle>::new();

        mcl_init(1, MCL_NULL.into());

        println!("Synchronous Test...");

        for i in 0..nreqs {
            hdls.push(mcl_task_create());

            if mcl_null(hdls[i]) == 1 {
                println!("Error submitting request {} ", i);
                continue;
            }

            if mcl_wait(hdls[i]) != 0 {
                println!("Request {} timed out!", i);
            }
        }

        for i in 0..nreqs {
            if (*hdls[i]).status != MCL_REQ_COMPLETED.into() {
                println!("Request {} status={}", i, (*hdls[i]).status);
                ret = 1;
            }
        }

        for i in 0..nreqs {
            mcl_hdl_free(hdls[i]);
        }

        hdls.clear();

        println!("Asynchronous Test...");

        for i in 0..nreqs {
            hdls.push(mcl_task_create());

            if mcl_null(hdls[i]) == 1 {
                println!("Error submitting request {} ", i);
                continue;
            }
        }
        mcl_wait_all();

        for i in 0..nreqs {
            if (*hdls[i]).status != MCL_REQ_COMPLETED.into() {
                println!("Request {} status={}", i, (*hdls[i]).status);
                ret = 1;
            }
        }

        for i in 0..nreqs {
            mcl_hdl_free(hdls[i]);
        }

        mcl_finit();
        assert_eq!(ret, 0);
    }
}
