// use mcl_rs;
// use rand::Rng;

// fn add_seq(x: &Vec::<i32>, y: &Vec::<i32>, z: &mut Vec::<i32>) {

//     for i in 0..z.len(){
//         z[i] = x[i] + y[i];
//     }
// }

// fn add_mcl(x: &Vec::<i32>, y: &Vec::<i32>, z: &mut Vec::<i32>, reps: usize, sync: &bool) {

//     let mut hdls : Vec::<mcl_rs::TaskHandle> = Vec::new();

//     for i in 0..reps {
//         let size : u32 = z.len() as u32;
//         let pes: [u64; 3] = [size as u64, 1, 1];
//         let props = mcl_rs::TaskBinProps::new(0, 4, "");

//         hdls.push(
//             mcl_rs::Task::from("tests/vadd.cl", "vector_add", 4).compile()
//                 .add_binary("tests/vector_addition.xclbin", props)
//                 .arg(mcl_rs::TaskArg::output_slice(z))
//                 .arg(mcl_rs::TaskArg::input_slice(x))
//                 .arg(mcl_rs::TaskArg::input_slice(y))
//                 .arg(mcl_rs::TaskArg::input_scalar(&size))
//                 .dev(mcl_rs::DevType::FPGA)
//                 .exec(pes)
//                 .wait();   
//         );
        
//         if *sync {
//             hdls[i].wait();
//         }
//     }

//     if !*sync {
//         for i in 0..reps {
//             hdls[i].wait();
//         }
//     }
    
// }

// #[test]
// fn vadd() {

//     let workers = 1;
//     let vec_size = 128;
//     let reps = 4;
//     let sync = false;

//     // Initialize mcl. No need to bind return element, we only need it to drop when it should.
//     let env = mcl_rs::MclEnvBuilder::new()
//         .num_workers(workers)
//         .initialize();
    
//     let mut rng = rand::thread_rng();

//     // Generate x and y arrays of size vec_size and initialize with random numbers in [0, 100)
//     let x: Vec::<i32> = (0..vec_size).map(|_| {rng.gen_range(0..100)}).collect();
//     let y: Vec::<i32> = (0..vec_size).map(|_| {rng.gen_range(0..100)}).collect();

//     // Allocate the z vectors that will hold the results.
//     let mut z: Vec::<i32> = vec![0; vec_size];
//     let mut z_seq: Vec::<i32> = vec![0; vec_size];

//     add_seq(&x, &y, &mut z_seq);

//     println!("Async mcl add");
//     add_mcl(&x, &y, &mut z, reps, &sync);
//     assert_eq!(z_seq, z);

//     let mut z = vec![0; vec_size];
//     let sync = true;



//     println!("Sync mcl add");
//     add_mcl(&x, &y, &mut z, reps, &sync);
//     assert_eq!(z_seq, z);
// }
