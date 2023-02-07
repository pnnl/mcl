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

async fn gemm_mcl(env: &mcl_rs::Mcl, a: &Vec::<i32>, b: &Vec::<i32>, cs: &mut Vec<Vec::<i32>>, n: &usize, sync: &bool) {

    let mut hdls = Vec::new();
    env.load_prog("tests/gemmN.cl", mcl_rs::PrgType::Src);

    for c in cs.iter_mut(){

        let pes: [u64; 3] = [*n as u64, *n as u64, 1];
        
        hdls.push(
            env.task("gemmN", 4)
                .arg(mcl_rs::TaskArg::input_slice(a))
                .arg(mcl_rs::TaskArg::input_slice(b))
                .arg(mcl_rs::TaskArg::input_scalar(n))
                .arg(mcl_rs::TaskArg::output_slice(c))
                .dev(mcl_rs::DevType::ANY)
                .exec(pes)
        );
        
        if *sync {
            hdls.pop().expect("Task just pushed to vec").await;
        }
    }
    if !*sync {
        futures::future::join_all(hdls).await;
    }
   
}

static REPS: usize = 100;

#[test]
fn gemm() {

    let workers = 2;
    let n  = 128;
    let nn = n * n;
    let mut reps = REPS;
    reps += 10;
    let sync = false;
    
    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(workers)
        .initialize();

    
    let mut rng = rand::thread_rng();

    // Generate a and b matrices of size NxN and initialize with random numbers in [0, 100)
    let a: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();
    let b: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();

    // Allocate the c matrix that will hold the results.
    let mut cs: Vec::<Vec::<i32>> = vec![vec![0; nn];reps]; //we need a buffer for each result 
    let mut c_seq: Vec::<i32> = vec![0; nn];

    let start = std::time::Instant::now();
    gemm_seq(&a, &b, &mut c_seq, n);
    let seq_time = start.elapsed().as_secs_f32();

    println!("Async mcl gemm");
    let start = std::time::Instant::now();
    futures::executor::block_on(gemm_mcl(&env, &a, &b, &mut cs, &n, &sync));
    let async_time = start.elapsed().as_secs_f32();
    for c in cs {
        assert_eq!(c_seq, c);
    }

    let mut cs = vec![vec![0; nn];reps]; //we need a buffer for each result 
    let sync = true;



    println!("Sync mcl gemm");
    let start = std::time::Instant::now();
    futures::executor::block_on(gemm_mcl(&env, & a, & b, &mut cs, &n, &sync));
    let sync_time = start.elapsed().as_secs_f32();
    for c in cs {
        assert_eq!(c_seq, c);
    }

    println!("Seq: {seq_time}, Async: {async_time} Sync: {sync_time}");
}
