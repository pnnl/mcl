
use rand::Rng;
use std::fs::File;
use std::io::prelude::*;
use std::io::BufReader;
use std::convert::TryInto;
fn get_c(vec: &Vec<u8>, i: &mut usize) -> u8 {
    let c = vec[*i];
    *i += 1;

    return c;
}


fn read_pgm_file(path: &str, buffer: &mut Vec::<f32>) -> std::io::Result<()> {

    let mut f = File::open(path)?;
    let mut contents = Vec::new();
    f.read_to_end(&mut contents)?;

    let mut idx = 0;
    if get_c(&contents, &mut idx) as char != 'P' {
        return Err(std::io::Error::new(std::io::ErrorKind::Other, "Error invalid pgm file (only accepts P5)"));
    }
    if get_c(&contents, &mut idx) -48 != 5 {
        return Err(std::io::Error::new(std::io::ErrorKind::Other, "Error invalid pgm file (only accepts P5)"));
    }

    while get_c(&contents, &mut idx) as char != '\n' {}
    while get_c(&contents, &mut idx) as char == '#' {
        while get_c(&contents, &mut idx) as char != '\n' {
        } 
    }
    idx -= 1;
    let mut h: usize = 0;
    let mut w: usize = 0;
    let mut val = get_c(&contents, &mut idx);
    while val as char != ' ' {
        h = h * 10 + (val - 48) as usize;
        val = get_c(&contents, &mut idx);
    }

    let mut val = get_c(&contents, &mut idx);
    while val as char != '\n' && val as char != ' '{
        w = w * 10 + (val - 48) as usize;
        val = get_c(&contents, &mut idx);
    }

    if (w * h) as usize != buffer.len() {
        return Err(std::io::Error::new(std::io::ErrorKind::Other, "Image size does not much buffer size"));
    }

    for i in 0..buffer.len() {
        buffer[i]  = get_c(&contents, &mut idx).into();
    }

    // Print image
    // for i in 0..28 {
    //     for j in 0..28 {
    //         print!("{:3} ", buffer[i * 28 + j]);
    //     }
    //     println!("");
    // }





    Ok(())
}

#[cfg(feature="versal")]
#[test]
fn nvdla() {
    const REP : usize = 2;
    const NUM_DIGITS: usize = 10;
    const digits_per_test: usize = 2;
    const IMGSIZE: usize = 28 * 28;
    let mut model_path = "tests/nvdla_data/mnist.nvdla";
    let pgm_dir = String::from("tests/nvdla_data/");
    // let mut hdls : Vec::<mcl_rs::TaskHandle> = Vec::new();
    let mut vin : Vec::<Vec::<f32>> = Vec::new();
    let mut vout : Vec::<Vec::<f32>> = Vec::new();

    const num_workers: usize = 1;

    let env = mcl_rs::MclEnvBuilder::new()
        .num_workers(num_workers)
        .initialize();

    for i in 0..env.get_ndev() {
        let dev = env.get_dev(i);
        if dev.name == "NVIDIA XAVIER" {
            model_path = "nvdla_data/xavier_mnist.nvdla";
        }
    }

    for i in 0..NUM_DIGITS {
        vin.push(vec![0.0; IMGSIZE]);
        vout.push(vec![0.0; 10]);
        let img_name = String::from(&pgm_dir) + &i.to_string()+".pgm";

        read_pgm_file(&img_name, &mut vin[i]).expect("Error");
    }


    let pes : [u64; 3] = [1, 1, 1];
    let mut rng = rand::thread_rng();
    for r in 0..REP {
        for i in 0..digits_per_test {
            let digit  = rng.gen_range(0..10);
            println!("input digit: {}", digit);
            mcl_rs::Task::from(model_path, "DLA_MNIST", 2)
                .flags(mcl_rs::PrgFlag::BIN)
                .compile()
                .arg(mcl_rs::TaskArg::input_slice(&vin[digit]))
                .arg(mcl_rs::TaskArg::output_slice(&mut vout[i]))
                .dev(mcl_rs::DevType::NVDLA)
                .exec(pes)
                .wait();
        }
    }

    for i in 0..digits_per_test {
        println!("Prediction for inference {}", i);
        for j in 0..10 {
            if vout[i][j] > 0.001 {
                println!("{}", j);
            }
            vout[i][j] = 0.0;
        }
        println!("==============================================");
    }
}