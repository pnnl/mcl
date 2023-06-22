use mcl_rs::{DevType, Mcl, MclEnvBuilder, PrgType, TaskArg}; //imports the module
use std::time::Instant;

fn add_seq(x: &[i32], y: &[i32], z: &mut [i32]) {
    for i in 0..z.len() {
        z[i] = x[i] + y[i];
    }
}

async fn add_mcl(env: &Mcl, x: &Vec<i32>, y: &Vec<i32>, z: &mut Vec<i32>) {
    let size: u32 = z.len() as u32;
    let pes: [u64; 3] = [size as u64, 1, 1];

    // create a task handle containing the task + arguments we want to run;
    let task_hdl = env.task("VADD", 3)
                .arg(TaskArg::output_slice(z))
                .arg(TaskArg::input_slice(x))
                .arg(TaskArg::input_slice(y))
                .dev(DevType::ANY);
    //now we want to execute this task
    //the exec function is itself an async function
    //meaning that it returns a future we need to "drive"
    //because we are currently in an async function, we can simply
    //"await" the result of this future here.
    task_hdl.exec(pes).await; //try removing the await and re-build/re-run to see what happens!
}

fn main() {
    let mcl = MclEnvBuilder::new().num_workers(2).initialize();
    mcl.load_prog("src/vadd.cl", PrgType::Src);

    let vec_size = 1000000;

    let x: Vec<i32> = vec![1; vec_size]; //vector of ones
    let y: Vec<i32> = vec![2; vec_size]; //vector of twos
    let mut z_seq = vec![0; vec_size]; //the compiler will infer the correct type

    let mut timer = Instant::now();
    
    add_seq(&x, &y, &mut z_seq);
    
    println!(
        "seq time: {} z[0..10] = {:?} ",
        timer.elapsed().as_secs_f64(),
        &z_seq[0..10]
    );

    let mut z_mcl = vec![0; vec_size]; //the compiler will infer the correct type
    timer = Instant::now();

    //add_mcl is an "async" fuction, this means it returns something
    //called a future, which represents some work do be performed sometime in the future.
    //Rust has lazy futures, meaning they dont actually do anything until you "drive" them
    //For this example we will simply "block_on" our task.
    let task = add_mcl(&mcl, &x, &y, &mut z_mcl);
    
    //this is how we actually drive our async task!
    futures::executor::block_on(task);//comment out this line and see what the compiler says!
    println!(
        "mcl time: {} z[0..10] = {:?} ",
        timer.elapsed().as_secs_f64(),
        &z_mcl[0..10]
    );
}