use mcl_rs::*;
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

async fn gemm_mcl(env: &mcl_rs::Mcl, a: &Vec::<i32>, b: &Vec::<i32>, cs: &mut Vec<Vec::<i32>>, n: &usize, sync: &bool, test_type: u32) {

    let mut hdls = Vec::new();

    if test_type == 3 {
        env.transfer(2, 1)
            .arg(mcl_rs::TaskArg::input_slice(a).resident(true))
            .arg(mcl_rs::TaskArg::input_slice(b).resident(true))
            .exec().await;
    }

    for c in cs.iter_mut() {

        let pes: [u64; 3] = [*n as u64, *n as u64, 1];
        
        hdls.push(
            env.task("gemmN", 4)
                .arg(mcl_rs::TaskArg::input_slice(a).resident(test_type == 1 || test_type == 3).invalid(test_type == 2))
                .arg(mcl_rs::TaskArg::input_slice(b).resident(test_type == 1 || test_type == 3).invalid(test_type == 2))
                .arg(mcl_rs::TaskArg::input_scalar(n))
                .arg(mcl_rs::TaskArg::output_slice(c).resident(test_type == 1 || test_type == 3).invalid(test_type == 2))
                .exec(pes)
        );
        
        if *sync {
            hdls.pop().expect("Task just pushed to vec").await;
        }
    }

    if !*sync {
        futures::future::join_all(hdls).await;
    }

    if test_type == 3 {
        env.transfer(2, 1)
            .arg(TaskArg::input_slice(a).resident(true).done(true))
            .arg(TaskArg::input_slice(b).resident(true).done(true))
            .exec().await;
    }
}



#[test]
fn resdata() {

    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(1)
        .initialize();
    env.load_prog("tests/gemmN.cl", mcl_rs::PrgType::Src);
    let n  = 16;
    let nn = n * n;
    let reps = 100;
    let sync = false;
    

    
    let mut rng = rand::thread_rng();

    // Generate a and b matrices of size NxN and initialize with random numbers in [0, 100)
    let a: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();
    let b: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();

    // Allocate the c matrix that will hold the results.
    let mut cs: Vec<Vec::<i32>> = vec![vec![0; nn];reps];
    let mut c_seq: Vec::<i32> = vec![0; nn];

    gemm_seq(&a, &b, &mut c_seq, n);

    // Test 0
    futures::executor::block_on(gemm_mcl(&env, &a, &b, &mut cs, &n, &sync, 3));

    assert_eq!(c_seq, cs[0]);

    // Test 1
    let mut cs = vec![vec![0; nn];reps];
    futures::executor::block_on(gemm_mcl(&env, &a, &b, &mut cs, &n, &sync, 1));
    
    assert_eq!(c_seq, cs[0]);
    
    // Test 2
    let mut cs = vec![vec![0; nn];reps];
    futures::executor::block_on(gemm_mcl(&env, &a, &b, &mut cs, &n, &sync, 2));

    assert_eq!(c_seq, cs[0]);

    // Test 3
    let mut cs = vec![vec![0; nn];reps];
    futures::executor::block_on(gemm_mcl(&env, &a, &b, &mut cs, &n, &sync, 3));

    assert_eq!(c_seq, cs[0]);
}