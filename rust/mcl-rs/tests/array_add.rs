use mcl_rs;
use rand::Rng;

fn add_seq(x: &Vec::<i32>, y: &Vec::<i32>, z: &mut Vec::<i32>) {

    for i in 0..z.len(){
        z[i] = x[i] + y[i];
    }
}

fn add_mcl(env: &mcl_rs::Mcl, x: &Vec::<i32>, y: &Vec::<i32>, z: &mut Vec::<i32>, reps: usize, sync: &bool) {

    let mut hdls : Vec::<mcl_rs::TaskHandle> = Vec::new();

    for i in 0..reps {
        let size : u32 = z.len() as u32;
        let pes: [u64; 3] = [size as u64, 1, 1];
        hdls.push(
            env.task("tests/vadd.cl", "VADD", 3)
                .arg(mcl_rs::TaskArg::output_slice(z))
                .arg(mcl_rs::TaskArg::input_slice(x))
                .arg(mcl_rs::TaskArg::input_slice(y))
                .dev(mcl_rs::DevType::CPU)
                .exec(pes)
                // .wait()
        );

        // task_set_arg(&hdls[i], 0, &mut x[..],ArgOpt::BUFFER| ArgOpt::Input);
        // task_set_arg(&hdls[i], 1, &mut y[..],ArgOpt::BUFFER| ArgOpt::Input);
        // task_set_arg(&hdls[i], 2, &mut z[..],ArgOpt::BUFFER| ArgOpt::OUTPUT);
        // exec(&hdls[i], &mut pes, &mut les, DevType::GPU);
        
        if *sync {
            hdls[i].wait();
        }
    }

    if !*sync {
        for i in 0..reps {
            hdls[i].wait();
        }
    }
    
}

#[test]
fn vadd() {

    let workers = 1;
    let vec_size = 128;
    let reps = 4;
    let sync = false;

    // Initialize mcl. No need to bind return element, we only need it to drop when it should.
    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(workers)
        .initialize();
    
    let mut rng = rand::thread_rng();

    // Generate x and y arrays of size vec_size and initialize with random numbers in [0, 100)
    let x: Vec::<i32> = (0..vec_size).map(|_| {rng.gen_range(0..100)}).collect();
    let y: Vec::<i32> = (0..vec_size).map(|_| {rng.gen_range(0..100)}).collect();

    // Allocate the z vectors that will hold the results.
    let mut z: Vec::<i32> = vec![0; vec_size];
    let mut z_seq: Vec::<i32> = vec![0; vec_size];

    add_seq(&x, &y, &mut z_seq);

    println!("Async mcl add");
    add_mcl(&env, &x, &y, &mut z, reps, &sync);
    assert_eq!(z_seq, z);

    let mut z = vec![0; vec_size];
    let sync = true;



    println!("Sync mcl add");
    add_mcl(&env, &x, &y, &mut z, reps, &sync);
    assert_eq!(z_seq, z);
}
