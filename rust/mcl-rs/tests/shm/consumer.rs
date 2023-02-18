use mcl_rs;

async fn start_iteration(env: &mcl_rs::Mcl, buffer: mcl_rs::SharedMemBuffer) -> Vec<u32>{
    let a = vec![2;buffer.len()];
    let mut b = vec![0;buffer.len()];
    let pes: [u64; 3] = [buffer.len() as u64, 1, 1];

    let mut task = env.task("VADD",3)
        .arg(mcl_rs::TaskArg::output_slice(&mut b)) // output
        .arg(mcl_rs::TaskArg::input_slice(&a));
    
    task =  unsafe {task.arg_shared_buffer(buffer)};
    task.dev(mcl_rs::DevType::ANY)
        .exec(pes).await;
    
    b
}

pub fn start(pid: i32, size: usize, iterations: usize){
    std::thread::sleep(std::time::Duration::from_millis(1000)); //give some time for the producer the construct tasks and data
    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(2)
        .initialize();
    env.load_prog("tests/vadd.cl", mcl_rs::PrgType::Src);

    let shared_data = env.attach_shared_buffer(mcl_rs::TaskArg::inout_shared::<u32>("mcl_shm_test",size*iterations));
    
    let hdls = (0..iterations).map(|i| {
        let shared_task = env.attach_shared_task(pid,i as u32);
        let buffer = shared_data.sub_buffer(i*size..(i+1)*size);
        let my_task = start_iteration(&env,buffer);
        async move {
            shared_task.wait().await;
            my_task.await
        }
    }).collect::<Vec<_>>();

    futures::executor::block_on(
        async move {
            let results = futures::future::join_all(hdls).await;

            for r in results{
                if !r.iter().all(|&elem| elem == 5){
                    println!("Failed to verify results from shared mem test");
                    println!("Every elemenet should be 5: {r:?}");
                }
            }
        }
    );
}