use mcl_rs;
use rand::Rng;

fn gemm_seq(a: &Vec<i32>, b: &Vec<i32>, c: &mut Vec<i32>, n: usize) {
    for i in 0..n {
        for j in 0..n {
            for k in 0..n {
                c[i * n + j] += a[i * n + k] * b[k * n + j]
            }
        }
    }
}

async fn gemm_mcl(
    env: &mcl_rs::Mcl,
    a: &Vec<i32>,
    b: &Vec<i32>,
    cs: &mut Vec<Vec<i32>>,
    n: &usize,
    sync: &bool,
) {
    let mut hdls = Vec::new();

    // let tile_size = a.len()/num_tiles;
    let tile_size = 64 * n; // * std::mem::size_of<i32>;
    let num_tiles = a.len() / tile_size;

    println!("{:?} {num_tiles} {tile_size}", a.len());
    let pes: [u64; 3] = [64 as u64, *n as u64, 1];
    println!("cs {:?}", cs.len());
    for c in cs.iter_mut() {
        println!("here");

        let buffer = env.register_buffer(
            mcl_rs::TaskArg::output_slice(c)
                .resident(true)
                .dynamic(true),
        ); //this is reference counted so it will remain active as long as this handle remains in scope, or and task handle using it remains in scope
        for j in 0..num_tiles {
            let s_i = tile_size * j;
            let e_i = s_i + tile_size;
            let new_a = &a[s_i..e_i];
            println!("new_a {:?}", new_a.as_ptr());
            hdls.push(
                env.task("gemmN", 4)
                    .arg(mcl_rs::TaskArg::input_slice(new_a).resident(true))
                    .arg(mcl_rs::TaskArg::input_slice(b).resident(true))
                    .arg(mcl_rs::TaskArg::input_scalar(n))
                    .arg_buffer(buffer.sub_buffer(s_i..e_i)) //we do runtime reference counting to ensure memory safety
                    .dev(mcl_rs::DevType::ANY)
                    .exec(pes),
            );
            if *sync {
                hdls.pop().expect("Task just pushed to vec").await;
            }
        }
        // buffer now out of scope, but it remains registered as handles containing it now are in "hdls"
    }
    if !*sync {
        futures::future::join_all(hdls).await;
    }
    // all references to the registered buffers have been dropped at this point so they will automatically be unregistered
}

static REPS: usize = 10;

#[test]
fn tiled_gemm() {
    let workers = 2;
    let n = 128;
    let nn = n * n;
    let reps = REPS;
    // reps += 10;
    let sync = true;

    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(workers)
        .initialize();
    env.load_prog("tests/gemmN.cl", mcl_rs::PrgType::Src);

    let mut rng = rand::thread_rng();

    // Generate a and b matrices of size NxN and initialize with random numbers in [0, 100)
    let a: Vec<i32> = (0..nn)
        .enumerate()
        .map(|(_, _)| rng.gen_range(0..100))
        .collect();
    let b: Vec<i32> = (0..nn)
        .enumerate()
        .map(|(_, _)| rng.gen_range(0..100))
        .collect();

    // Allocate the c matrix that will hold the results.
    let mut cs: Vec<Vec<i32>> = vec![vec![0; nn]; reps]; //we need a buffer for each result
    let mut c_seq: Vec<i32> = vec![0; nn];

    let start = std::time::Instant::now();
    gemm_seq(&a, &b, &mut c_seq, n);
    let seq_time = start.elapsed().as_secs_f32();

    println!("{:?} {:?}", &c_seq[0..10], &c_seq[(nn - 10)..nn]);

    println!("Sync mcl gemm");
    let start = std::time::Instant::now();
    futures::executor::block_on(gemm_mcl(&env, &a, &b, &mut cs, &n, &sync));
    let sync_time = start.elapsed().as_secs_f32();
    // println!("{:?}",cs);
    for c in cs {
        // for (i,(e1,e2)) in c.iter().zip(c_seq.iter()).enumerate(){
        //     assert_eq!(e1, e2,"failed at index {i}");
        // }
        assert_eq!(c_seq, c);
    }

    let mut cs = vec![vec![0; nn]; reps]; //we need a buffer for each result
    let sync = false;

    println!("Async mcl gemm");
    let start = std::time::Instant::now();
    futures::executor::block_on(gemm_mcl(&env, &a, &b, &mut cs, &n, &sync));
    let async_time = start.elapsed().as_secs_f32();
    for c in cs {
        assert_eq!(c_seq, c);
    }

    println!("Seq: {seq_time}, Async: {async_time} Sync: {sync_time}");
}
