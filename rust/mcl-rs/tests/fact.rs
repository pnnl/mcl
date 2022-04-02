use mcl_rs;
use rand::Rng;

fn fact_seq(v_in: &Vec::<u64>, v_out: &mut Vec::<u64>) {

    for i in 0..v_in.len() {
        let mut f = 1;
    
        for j in 1..=v_in[i] {
            f = f * j; 
        }
        v_out[i] = f;
    }
}

fn fact_mcl(env: &mcl_rs::Mcl, v_in: &Vec::<u64>, v_out: &mut Vec::<u64>, reps: usize, sync: &bool) {

    let mut hdls : Vec::<mcl_rs::TaskHandle> = Vec::new();

    for i in 0..reps {

        let pes: [u64; 3] = [1, 1, 1];

        let hdl = env.task("tests/fact.cl", "FACT", 2)
            .arg(mcl_rs::TaskArg::input_slice(&v_in[i..i+1]))
            .arg(mcl_rs::TaskArg::output_slice(&mut v_out[i..i+1]))
            .dev(mcl_rs::DevType::CPU)
            .exec(pes);
        hdls.push(hdl);

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
fn fact() {
    
    let workers = 1;
    let reps = 40;
    let vec_size  = reps;
    let sync = true;
    
    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(workers)
        .initialize();

    
    let mut rng = rand::thread_rng();

    // Generate v_in, v_out and v_out_seq arrays of size vec_size and initialize with 0
    let mut v_in: Vec::<u64> = (0..vec_size).map(|_| {rng.gen_range(1..10)}).collect();
    let mut v_out: Vec::<u64> = vec![0; vec_size];
    let mut v_out_seq: Vec::<u64> = vec![0; vec_size];


    fact_seq(&v_in, &mut v_out_seq);

    println!("Sync mcl fact");
    fact_mcl(&env,&v_in, &mut v_out, reps, &sync);
    assert_eq!(v_out_seq, v_out);

    let mut v_out: Vec::<u64> = vec![0; vec_size];
    let sync = false;



    println!("Async mcl fact");
    fact_mcl(&env,&mut v_in, &mut v_out, reps, &sync);
    assert_eq!(v_out_seq, v_out);
}
