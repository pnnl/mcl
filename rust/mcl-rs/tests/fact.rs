use mcl_rs;
use rand::Rng;

fn fact_seq(v_in: &Vec<u64>, v_out: &mut Vec<u64>) {
    for i in 0..v_in.len() {
        let mut f = 1;

        for j in 1..=v_in[i] {
            f = f * j;
        }
        v_out[i] = f;
    }
}

async fn fact_mcl(env: &mcl_rs::Mcl, v_in: &Vec<u64>, v_out: &mut Vec<u64>, sync: &bool) {
    let mut hdls = Vec::new();
    env.load_prog("tests/fact.cl", mcl_rs::PrgType::Src);
    for (i, o) in v_in.iter().zip(v_out.iter_mut()) {
        let pes: [u64; 3] = [1, 1, 1];

        let hdl = env
            .task("FACT", 2)
            .arg(mcl_rs::TaskArg::input_scalar(i))
            .arg(mcl_rs::TaskArg::output_scalar(o))
            .dev(mcl_rs::DevType::ANY)
            .exec(pes);
        hdls.push(hdl);

        // exec(&hdls[i], &mut pes, &mut les, DevType::GPU);

        if *sync {
            hdls.pop().expect("Handle was just pushed to vec").await;
        }
    }
    if !*sync {
        futures::future::join_all(hdls).await;
    }
}

#[test]
fn fact() {
    let workers = 1;
    let reps = 2;
    let vec_size = reps;
    let sync = true;

    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(workers)
        .initialize();

    let mut rng = rand::thread_rng();

    // Generate v_in, v_out and v_out_seq arrays of size vec_size and initialize with 0
    let mut v_in: Vec<u64> = (0..vec_size).map(|_| rng.gen_range(1..10)).collect();
    let mut v_out: Vec<u64> = vec![0; vec_size];
    let mut v_out_seq: Vec<u64> = vec![0; vec_size];

    fact_seq(&v_in, &mut v_out_seq);

    println!("Sync mcl fact");
    futures::executor::block_on(fact_mcl(&env, &v_in, &mut v_out, &sync));
    assert_eq!(v_out_seq, v_out);

    let mut v_out: Vec<u64> = vec![0; vec_size];
    let sync = false;

    println!("Async mcl fact");
    futures::executor::block_on(fact_mcl(&env, &mut v_in, &mut v_out, &sync));
    assert_eq!(v_out_seq, v_out);
}
