use mcl_rs;
use rand::Rng;

fn saxpy_seq(a: &i32, x: &Vec::<i32>, y: &Vec::<i32>, z: &mut Vec::<i32>) {

    for i in 0..z.len(){
        z[i] = a * x[i] + y[i];
    }
}

fn saxpy_mcl(env: &mcl_rs::Mcl, a: &i32, x: &Vec::<i32>, y: &Vec::<i32>, z: &mut Vec::<i32>, reps: usize, sync: &bool) {

    let mut hdls : Vec::<mcl_rs::TaskHandle> = Vec::new();
 
    for i in 0..reps {

        let size : u64 = z.len() as u64;
        let pes: [u64; 3] = [size, 1, 1];

        // hdls.push(task_init("tests/saxpy.cl", "SAXPY", 4, "", 0));
        hdls.push(
            env.task("tests/saxpy.cl", "SAXPY", 4)
                .arg(mcl_rs::TaskArg::input_slice(x))
                .arg(mcl_rs::TaskArg::input_scalar(a))
                .arg(mcl_rs::TaskArg::input_slice(y))
                .arg(mcl_rs::TaskArg::output_slice(z))
                .dev(mcl_rs::DevType::CPU)
                .exec(pes) 
        );

        // task_set_arg(&hdls[i], 0, &mut x[..], ArgType::BUFFER| ArgOpt::INPUT);
        // task_set_arg(&hdls[i], 1, std::slice::from_mut(a), ArgType::SCALAR| ArgOpt::INPUT);
        // task_set_arg(&hdls[i], 2, &mut y[..], ArgType::BUFFER| ArgOpt::INPUT);
        // task_set_arg(&hdls[i], 3, &mut z[..], ArgType::BUFFER| ArgOpt::OUTPUT);
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
fn saxpy() {
    let workers = 2;
    let vec_size = 128;
    let reps = 100;
    let sync = false;

    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(workers)
        .initialize();

    
    let mut rng = rand::thread_rng();
    let a  = rng.gen_range(0..100);

    // Generate x and y arrays of size vec_size and initialize with random numbers in [0, 100)
    let x: Vec::<i32> = (0..vec_size).map(|_| {rng.gen_range(0..100)}).collect();
    let y: Vec::<i32> = (0..vec_size).map(|_| {rng.gen_range(0..100)}).collect();

    // Allocate the z vectors that will hold the results.
    let mut z: Vec::<i32> = vec![0; vec_size];
    let mut z_seq: Vec::<i32> = vec![0; vec_size];

    saxpy_seq(&a, &x, &y, &mut z_seq);

    println!("Async mcl SAXPY");
    saxpy_mcl(&env,&a, &x, &y, &mut z, reps, &sync);
    assert_eq!(z_seq, z);

    let mut z = vec![0; vec_size];
    let sync = true;



    println!("Sync mcl SAXPY");
    saxpy_mcl(&env, &a, &x, &y, &mut z, reps, &sync);
    assert_eq!(z_seq, z);
}