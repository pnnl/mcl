use mcl_rs;
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

fn gemm_mcl(env: &mcl_rs::Mcl, a: &Vec::<i32>, b: &Vec::<i32>, c: &mut Vec::<i32>, n: &usize, reps: usize, sync: &bool) {

    let mut hdls : Vec::<mcl_rs::TaskHandle> = Vec::new();

    for i in 0..reps {

        let pes: [u64; 3] = [*n as u64, *n as u64, 1];
        
        hdls.push(
            env.task("tests/gemmN.cl", "gemmN", 4)
                .arg(mcl_rs::TaskArg::input_slice(a))
                .arg(mcl_rs::TaskArg::input_slice(b))
                .arg(mcl_rs::TaskArg::input_scalar(n))
                .arg(mcl_rs::TaskArg::output_slice(c))
                .dev(mcl_rs::DevType::CPU)
                .exec(pes)
        );

        // assert_eq!(mcl_task_set_kernel(hdls[i], kernel_path.into_raw(), kernel_name.into_raw(), 4, empty.into_raw(), 0),0);
        // task_set_arg(&hdls[i], 0, &mut a[..],ArgOpt::BUFFER| ArgOpt::Input);
        // task_set_arg(&hdls[i], 1, &mut b[..],ArgOpt::BUFFER| ArgOpt::Input);
        // task_set_arg(&hdls[i], 2, std::slice::from_mut(n),ArgOpt::SCALAR| ArgOpt::Input);
        // task_set_arg(&hdls[i], 3, &mut c[..],ArgOpt::BUFFER| ArgOpt::OUTPUT);
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
fn gemm() {

    let workers = 2;
    let n  = 128;
    let nn = n * n;
    let reps = 10;
    let sync = false;
    
    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(workers)
        .initialize();

    
    let mut rng = rand::thread_rng();

    // Generate a and b matrices of size NxN and initialize with random numbers in [0, 100)
    let a: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();
    let b: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();

    // Allocate the c matrix that will hold the results.
    let mut c: Vec::<i32> = vec![0; nn];
    let mut c_seq: Vec::<i32> = vec![0; nn];

    gemm_seq(&a, &b, &mut c_seq, n);

    println!("Async mcl gemm");
    gemm_mcl(&env, &a, &b, &mut c, &n, reps, &sync);
    assert_eq!(c_seq, c);

    let mut c = vec![0; nn];
    let sync = true;



    println!("Sync mcl gemm");
    gemm_mcl(&env, & a, & b, &mut c, &n, reps, &sync);
    assert_eq!(c_seq, c);
}
