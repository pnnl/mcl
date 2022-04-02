use mcl_rs;


#[test]
fn discovery()
{
    let env = mcl_rs::MclEnvBuilder::new()
                .initialize();

    let ndevs = env.get_ndev();

    println!("Found {} devives", ndevs);

    for i in 0..ndevs {
        let dev = env.get_dev(i);

        println!("{:?}", dev);
    }

}