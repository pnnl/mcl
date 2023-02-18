use mcl_rs::{DevType, Mcl, MclEnvBuilder, PrgType, TaskArg}; //imports the module
use std::time::Instant;

fn add_seq(x: &[i32], y: &[i32], z: &mut [i32]) {
    for i in 0..z.len() {
        z[i] = x[i] + y[i];
    }
}

async fn add_mcl(env: &Mcl, x: &Vec<i32>, y: &Vec<i32>, zs: &mut Vec<Vec<i32>>, sync: bool) {
    let size: u32 = zs[0].len() as u32;
    let pes: [u64; 3] = [size as u64, 1, 1];
    let tasks = zs
        .iter_mut()
        .map(|z| {
            env.task("VADD", 3)
                .arg(TaskArg::output_slice(z))
                .arg(TaskArg::input_slice(x))
                .arg(TaskArg::input_slice(y))
                .dev(DevType::ANY)
                .exec(pes)
        })
        .collect::<Vec<_>>(); //these are all futures so nothing actually happens until we await them
    if sync {
        for task in tasks {
            task.await; //launch and await each task sequentially
        }
    } else {
        futures::future::join_all(tasks).await; // launch and await each task "simultaneously"
    }
}

fn main() {
    let mcl = MclEnvBuilder::new().num_workers(2).initialize();
    mcl.load_prog("src/vadd.cl", PrgType::Src);

    let vec_size = 1000000;
    let reps = 1000;

    let x: Vec<i32> = vec![1; vec_size]; //vector of ones
    let y: Vec<i32> = vec![2; vec_size]; //vector of twos
    let mut z_seq = vec![0; vec_size]; //the compiler will infer the correct type

    let mut timer = Instant::now();
    for _i in 0..reps {
        add_seq(&x, &y, &mut z_seq);
    }
    println!(
        "seq time: {} z[0..10] = {:?} ",
        timer.elapsed().as_secs_f64(),
        &z_seq[0..10]
    );

    let mut z_mcl = vec![vec![0; vec_size]; reps]; //the compiler will infer the correct type
    timer = Instant::now();
    futures::executor::block_on(add_mcl(&mcl, &x, &y, &mut z_mcl, true)); //we need to actually drive our async function to do work
    println!(
        "mcl sync time: {} z[0..10] = {:?} ",
        timer.elapsed().as_secs_f64(),
        &z_mcl[0][0..10]
    );

    let mut z_mcl = vec![vec![0; vec_size]; reps]; //the compiler will infer the correct type
    timer = Instant::now();
    futures::executor::block_on(add_mcl(&mcl, &x, &y, &mut z_mcl, false)); //we need to actually drive our async function to do work
    println!(
        "mcl async time: {} z[0..10] = {:?} ",
        timer.elapsed().as_secs_f64(),
        &z_mcl[0][0..10]
    );
}
