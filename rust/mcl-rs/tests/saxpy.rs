use mcl_rs;
use rand::Rng;

fn saxpy_seq(a: &i32, x: &Vec::<i32>, y: &Vec::<i32>, z: &mut Vec::<i32>) {

    for i in 0..z.len(){
        z[i] = a * x[i] + y[i];
    }
}

async fn saxpy_mcl(env: &mcl_rs::Mcl, a: &i32, x: &Vec::<i32>, y: &Vec::<i32>, zs: &mut Vec<Vec::<i32>>, sync: &bool) {

    let mut hdls = Vec::new();
    env.load_prog("tests/saxpy.cl",mcl_rs::PrgType::Src);
 
    for z in zs.iter_mut(){

        let size : u64 = z.len() as u64;
        let pes: [u64; 3] = [size, 1, 1];

        hdls.push(
            env.task("SAXPY", 4)
                .arg(mcl_rs::TaskArg::input_slice(x))
                .arg(mcl_rs::TaskArg::input_scalar(a))
                .arg(mcl_rs::TaskArg::input_slice(y))
                .arg(mcl_rs::TaskArg::output_slice(z))
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
    let mut zs: Vec<Vec::<i32>> = vec![vec![0; vec_size];reps];
    let mut z_seq: Vec::<i32> = vec![0; vec_size];

    saxpy_seq(&a, &x, &y, &mut z_seq);

    println!("Async mcl SAXPY");
    futures::executor::block_on(saxpy_mcl(&env,&a, &x, &y, &mut zs, &sync));
    for z in zs {
        assert_eq!(z_seq, z);
    }

    let mut zs: Vec<Vec::<i32>> = vec![vec![0; vec_size];reps];
    let sync = true;



    println!("Sync mcl SAXPY");
    futures::executor::block_on(saxpy_mcl(&env, &a, &x, &y, &mut zs, &sync));
    for z in zs {
        assert_eq!(z_seq, z);
    }
}
