// use mcl_rs::*;
// use rand::Rng;

// fn gemm_seq(a: &Vec::<i32>, b: &Vec::<i32>, c: &mut Vec::<i32>, n: usize) {

//     for i in 0..n {
//         for j in 0..n {
//             for k in 0.. n {
//                 c[i * n + j] += a[i * n + k] * b[k * n + j] 
//             }
//         }
//     }
// }

// fn gemm_mcl(a: &Vec::<i32>, b: &Vec::<i32>, c: &mut Vec::<i32>, n: &usize, reps: usize, sync: &bool, test_type: u32) {

//     let mut hdls : Vec::<mcl_rs::TaskHandle> = Vec::new();

//     if test_type == 3 {
//         Transfer::new(2, 1)
//             .arg(mcl_rs::TaskArg::input_slice(a).resident(true))
//             .arg(mcl_rs::TaskArg::input_slice(b).resident(true))
//             .exec()
//             .wait();
//     }

//     for i in 0..reps {

//         let pes: [u64; 3] = [*n as u64, *n as u64, 1];
        
//         hdls.push(
//             mcl_rs::Task::from("tests/gemmN.cl", "gemmN", 4).compile()
//                 .arg(mcl_rs::TaskArg::input_slice(a).resident(test_type == 1 || test_type == 3).invalid(test_type == 2))
//                 .arg(mcl_rs::TaskArg::input_slice(b).resident(test_type == 1 || test_type == 3).invalid(test_type == 2))
//                 .arg(mcl_rs::TaskArg::input_scalar(n))
//                 .arg(mcl_rs::TaskArg::output_slice(c).resident(test_type == 1 || test_type == 3).invalid(test_type == 2))
//                 .exec(pes)
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

//     if test_type == 3 {
//         Transfer::new(2, 1)
//             .arg(TaskArg::input_slice(a).resident(true).done(true))
//             .arg(TaskArg::input_slice(b).resident(true).done(true))
//             .exec()
//             .wait();
//     }
// }



// // #[test]
// fn resdata() {

//     let env = mcl_rs::MclEnvBuilder::new()
//         .num_workers(1)
//         .initialize();
//     let n  = 16;
//     let nn = n * n;
//     let reps = 100;
//     let sync = true;
    

    
//     let mut rng = rand::thread_rng();

//     // Generate a and b matrices of size NxN and initialize with random numbers in [0, 100)
//     let a: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();
//     let b: Vec::<i32> = (0..nn).map(|_| {rng.gen_range(0..100)}).collect();

//     // Allocate the c matrix that will hold the results.
//     let mut c: Vec::<i32> = vec![0; nn];
//     let mut c_seq: Vec::<i32> = vec![0; nn];

//     gemm_seq(&a, &b, &mut c_seq, n);

//     // Test 0
//     gemm_mcl(&a, &b, &mut c, &n, reps, &sync, 3);

//     assert_eq!(c_seq, c);

//     // Test 1
//     c = vec![0; nn];
//     gemm_mcl(&a, &b, &mut c, &n, reps, &sync, 1);
    
//     assert_eq!(c_seq, c);
    
//     // Test 2
//     c = vec![0; nn];
//     gemm_mcl(&a, &b, &mut c, &n, reps, &sync, 2);

//     assert_eq!(c_seq, c);

//     // Test 3
//     c = vec![0; nn];
//     gemm_mcl(&a, &b, &mut c, &n, reps, &sync, 3);

//     assert_eq!(c_seq, c);
// }