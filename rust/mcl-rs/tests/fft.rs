use mcl_rs;
use rand::Rng;
use rustfft::num_complex::Complex;
use rustfft::FftPlanner;

fn rust_fft(reference: &mut Vec<Complex<f32>>) {
    let mut planner = FftPlanner::new();
    let fft = planner.plan_fft_forward(reference.len());
    fft.process(reference);
}

async fn fft_mcl_sync(
    env: &mcl_rs::Mcl,
    sources: &mut Vec<Vec<Complex<f32>>>,
    results: &mut Vec<Vec<Complex<f32>>>,
) {
    let size = sources[0].len();
    let pes: [u64; 3] = [(size / 2) as u64, 1, 1];

    let num_iters: usize = (size as f64).log2().floor() as usize;

    for (s, r) in sources.iter_mut().zip(results.iter_mut()) {
        for k in 0..num_iters {
            let p: i32 = 1 << k;
            if k == num_iters - 1 {
                fft_last(env, s, r, &p, pes).await;
            } else {
                fft_kernel(env, s, r, &p, pes).await;
            }
            std::mem::swap(s, r);
        }
        std::mem::swap(s, r);
    }
}

async fn fft_mcl_async(
    env: &mcl_rs::Mcl,
    sources: &mut Vec<Vec<Complex<f32>>>,
    results: &mut Vec<Vec<Complex<f32>>>,
) {
    let size = sources[0].len();
    let pes: [u64; 3] = [(size / 2) as u64, 1, 1];

    let num_iters: usize = (size as f64).log2().floor() as usize;

    let futures = sources
        .iter_mut()
        .zip(results.iter_mut())
        .map(|(s, r)| async move {
            for k in 0..num_iters {
                let p: i32 = 1 << k;
                if k == num_iters - 1 {
                    fft_last(env, s, r, &p, pes).await;
                } else {
                    fft_kernel(env, s, r, &p, pes).await;
                }
                std::mem::swap(s, r);
            }
            std::mem::swap(s, r);
        })
        .collect::<Vec<_>>();
    futures::future::join_all(futures).await;
}

async fn fft_kernel(
    env: &mcl_rs::Mcl,
    s: &Vec<Complex<f32>>,
    r: &mut Vec<Complex<f32>>,
    p: &i32,
    pes: [u64; 3],
) {
    env.task("fftRadix2Kernel", 3)
        .arg(mcl_rs::TaskArg::input_slice(s).resident(true).dynamic(true))
        .arg(mcl_rs::TaskArg::input_slice(r).resident(true).dynamic(true))
        .arg(mcl_rs::TaskArg::input_scalar(p))
        .dev(mcl_rs::DevType::ANY)
        .exec(pes)
        .await;
}

async fn fft_last(
    env: &mcl_rs::Mcl,
    s: &mut Vec<Complex<f32>>,
    r: &mut Vec<Complex<f32>>,
    p: &i32,
    pes: [u64; 3],
) {
    env.task("fftRadix2Kernel", 3)
        .arg(
            mcl_rs::TaskArg::input_slice(s)
                .resident(true)
                .dynamic(true)
                .done(true),
        )
        .arg(
            mcl_rs::TaskArg::inout_slice(r)
                .resident(true)
                .dynamic(true)
                .done(true),
        )
        .arg(mcl_rs::TaskArg::input_scalar(p))
        .dev(mcl_rs::DevType::ANY)
        .exec(pes)
        .await;
}

fn valid_result(result: &Vec<Complex<f32>>, reference: &Vec<Complex<f32>>) -> bool {
    result
        .iter()
        .zip(reference.iter())
        .fold(true, |valid, (x, y)| {
            // println!("{x:?} {y:?} {valid} {} {} {}",(x.x-y.x).abs(),  (x.y - y.y).abs(), (x.x-y.x).abs() <= 0.5 && (x.y - y.y).abs() <= 0.5);
            valid & ((x.re - y.re).abs() <= 0.5 && (x.im - y.im).abs() <= 0.5)
        })
}

#[test]
fn fft() {
    let workers = 1;
    let reps = 10;
    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(workers)
        .initialize();
    env.create_prog("tests/fft.cl", mcl_rs::PrgType::Src)
        .with_compile_args("-DSINGLE_PRECISION")
        .load();

    for exp in 1..21 {
        let dft_size = 2_i32.pow(exp) as usize;
        println!("DFT Size = {dft_size}");
        assert!(
            dft_size > 0 && (dft_size & (dft_size - 1) == 0),
            "FFT tests only works with powers of 2 Vec sizes"
        );

        let mut rng = rand::thread_rng();

        let mut reference = vec![Default::default(); dft_size];
        for i in 0..dft_size / 2 {
            reference[i] = Complex {
                re: rng.gen::<f32>(),
                im: rng.gen::<f32>(),
                // re: (i+1) as f32/((dft_size*2)/3)as f32 * 2.0 -1.0,
                // im: (i+1) as f32/((dft_size*2)/3)as f32 * 2.0 -1.0,
            };
            reference[i + dft_size / 2] = reference[i];
        }

        let mut sync_sources = vec![reference.clone(); reps];
        let mut async_sources = sync_sources.clone();
        let mut results = vec![vec![Default::default(); dft_size]; reps];

        let start = std::time::Instant::now();
        rust_fft(&mut reference);
        let ref_time = start.elapsed().as_secs_f32();

        println!("Sync mcl fft");
        let start = std::time::Instant::now();
        futures::executor::block_on(fft_mcl_sync(&env, &mut sync_sources, &mut results));
        let sync_time = start.elapsed().as_secs_f32();
        assert!(
            valid_result(&results[0], &reference),
            "SYNC fails for {dft_size}"
        );

        let mut results = vec![vec![Default::default(); dft_size]; reps];
        println!("Async mcl fft");
        let start = std::time::Instant::now();
        futures::executor::block_on(fft_mcl_async(&env, &mut async_sources, &mut results));
        let async_time = start.elapsed().as_secs_f32();
        assert!(
            valid_result(&results[0], &reference),
            "ASYNC fails for {dft_size}"
        );
        println!("Ref Time: {ref_time} Sync time: {sync_time}, Async time: {async_time}");
    }
}
