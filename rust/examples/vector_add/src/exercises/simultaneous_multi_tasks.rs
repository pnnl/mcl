use mcl_rs::{DevType, Mcl, MclEnvBuilder, PrgType, TaskArg}; //imports the module
use std::time::Instant;

fn add_seq(x: &[i32], y: &[i32], z: &mut [i32]) {
    for i in 0..z.len() {
        z[i] = x[i] + y[i];
    }
}


// Instead of creating and executing our tasks within an async block sequentially,
// we can create all the tasks first without executing them and then store them in a vector.
// We want the task to execute simultaneously so the each need their own output buffer.
async fn add_mcl(env: &Mcl, x: &Vec<i32>, y: &Vec<i32>, zs: &mut Vec<Vec<i32>>) {
    let size: u32 = zs[0].len() as u32;
    let pes: [u64; 3] = [size as u64, 1, 1];

    // Iterators are very common and efficient in Rust.
    // In this example we are mutably iterating over our output buffers
    // the "map" function allows us to convert our integer element "i" into some other type,
    // in our case we are converting it to a future returned from aour async exec() function.
    // finally we collect all the elements (which are now futures) into a vector (letting the compiler figure our the exact type!)
    let tasks = zs.iter_mut().map(|z| 
            env.task("VADD", 3)
                .arg(TaskArg::output_slice(z))
                .arg(TaskArg::input_slice(x))
                .arg(TaskArg::input_slice(y))
                .dev(DevType::ANY)
                .exec(pes) // we are not awaiting at this point so the returned value is just a future
    ).collect::<Vec<_>>(); //try changing z_mcl[i] to z_mcl[0]

    // now we need some way to drive our tasks, we could simply "block_on" each element in our tasks vector
    // but that still ends up sequentially executing each task. Instead we want to execute them simultaneously!
    // join_all is an async function that handles simultaneously "await"ing multiple futures automatically for us!
    futures::future::join_all(tasks).await;
}

fn main() {
    let mcl = MclEnvBuilder::new().num_workers(2).initialize();
    mcl.load_prog("src/vadd.cl", PrgType::Src);

    let vec_size = 1000000;
    let reps = 10;

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

    let  mut z_mcl = vec![vec![0; vec_size]; reps]; //now we need multiple output buffers for each task we want to execute!
    
    timer = Instant::now();

    let simultaneous_tasks = add_mcl(&mcl, &x, &y, &mut z_mcl);

    futures::executor::block_on(simultaneous_tasks); //finally we block on the simultaneous tasks future
    println!(
        "mcl async time: {} z[0..10] = {:?} ",
        timer.elapsed().as_secs_f64(),
        &z_mcl[0][0..10]
    );
}
