use mcl_rs::{DevType, TaskArg, TaskHandle, PrgType}; //imports the module
use std::time::Instant;

fn add_seq(x: &[i32], y: &[i32], z: &mut [i32]) {
    for i in 0..z.len() {
        z[i] = x[i] + y[i];
    }
}

fn add_mcl(mcl: &mcl_rs::Mcl, x: &[i32], y: &[i32], z: &mut [i32], reps: usize, sync: bool) {

    let size = z.len() as u64;
    let global_work_dims: [u64; 3] = [size as u64, 1, 1];
    let hdls = (0..reps).filter_map(|_| {
        let hdl =mcl.task( "VADD", 3)
            .arg(TaskArg::output_slice(z))
            .arg(TaskArg::input_slice(x))
            .arg(TaskArg::input_slice(y))
            .dev(DevType::ANY)
            .exec(global_work_dims.clone());
        if sync{
            hdl.wait();
            None
        }
        else{
            Some(hdl)
        }
    }
    ).collect::<Vec::<TaskHandle>>();

    //hdls is only used if sync is false
    for hdl in hdls{
        hdl.wait();
    }
}

fn main() {
    let mcl = mcl_rs::MclEnvBuilder::new().num_workers(10).initialize();

    let vec_size = 1000000;

    let x: Vec<i32> = (0..vec_size).map(|_| 1).collect(); //vector of ones
    let y: Vec<i32> = (0..vec_size).map(|_| 2).collect(); //vector of twos
    let mut z = vec![0; vec_size]; //the compiler will infer the correct type
    let mut z_mcl_sync = vec![0; vec_size]; //the compiler will infer the correct type
    let mut z_mcl_async = vec![0; vec_size]; //the compiler will infer the correct type

    let reps = 1000;

    let mut timer = Instant::now();
    for _i in 0..reps {
        add_seq(&x, &y, &mut z);
    }
    println!("seq time: {} z[0..10] = {:?} ",timer.elapsed().as_secs_f64(),&z[0..10]);


    mcl.create_prog("src/vadd.cl",PrgType::Src).load();
    timer = Instant::now();
    add_mcl(&mcl, &x, &y, &mut z_mcl_sync,reps, true);
    println!("mcl sync time: {} z[0..10] = {:?} ",timer.elapsed().as_secs_f64(),&z[0..10]);

    timer = Instant::now();
    add_mcl(&mcl, &x, &y, &mut z_mcl_async,reps, false);
    println!("mcl async time: {} z[0..10] = {:?} ",timer.elapsed().as_secs_f64(),&z[0..10]);
}
