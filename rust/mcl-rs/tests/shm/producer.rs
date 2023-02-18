use mcl_rs;

async fn start_iteration(env: &mcl_rs::Mcl, buffer: mcl_rs::SharedMemBuffer) {
    let a = vec![1; buffer.len()];
    let b = vec![2; buffer.len()];
    let pes: [u64; 3] = [buffer.len() as u64, 1, 1];

    unsafe {
        env.shared_task("VADD", 3)
            .arg_shared_buffer(buffer.clone()) //output
            .arg(mcl_rs::TaskArg::input_slice(&a))
            .arg(mcl_rs::TaskArg::input_slice(&b))
            .dev(mcl_rs::DevType::ANY)
            .exec(pes)
            .await;
    }
}

pub fn start(size: usize, iterations: usize) {
    let env = mcl_rs::MclEnvBuilder::new().num_workers(2).initialize();
    env.load_prog("tests/vadd.cl", mcl_rs::PrgType::Src);

    let shared_data = env.create_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>(
        "mcl_shm_test",
        size * iterations,
    ));

    let hdls = (0..iterations)
        .map(|i| start_iteration(&env, shared_data.sub_buffer(i * size..(i + 1) * size)))
        .collect::<Vec<_>>();

    futures::executor::block_on(futures::future::join_all(hdls));
    std::thread::sleep(std::time::Duration::from_millis(5000)); //so that we do not free our shared memory objects before the consumer attaches
}
