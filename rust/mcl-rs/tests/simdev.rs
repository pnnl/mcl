
#[cfg(feature="versal")]
#[test]
fn simdev() {
    const N: usize = 100;
    const rep:usize = 2;
    const num_workers: usize = 1;

    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(num_workers)
        .initialize();

    let mut a : Vec<i32> = vec![0; N * N];
    let mut b : Vec<i32> = vec![0; N * N];
    let mut c : Vec<i32> = vec![0; N * N];

    for i in 0..N {
        for j in 0..N {
            a[i* N + j] = j as i32;
            b[i* N + j] = j as i32;
        }
    }

    env.load_prog("",mcl_rs::PrgType::SIMDEV).load();

    let mut hdls : Vec::<mcl_rs::TaskHandle> = Vec::new();
    let pes : [u64; 3] = [1, 1, 1];
    for i in 0..rep {
        hdls.push(
            mcl_rs::Task::from("kernel0", 4)
                .compile()
                .arg(mcl_rs::TaskArg::input_slice(&a))
                .arg(mcl_rs::TaskArg::input_slice(&b))
                .arg(mcl_rs::TaskArg::input_scalar(&N))
                .arg(mcl_rs::TaskArg::output_slice(&mut c))
                .dev(mcl_rs::DevType::SIMDEV)
                .exec(pes)
        );

        hdls[i].wait();
    }
}