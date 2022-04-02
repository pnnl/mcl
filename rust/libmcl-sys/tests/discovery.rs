use libmcl_sys::*;
use std::ptr::null_mut;


#[test]
fn discovery()
{
    unsafe{
        assert_eq!(mcl_init(1, MCL_NULL.into()), 0);

        let ndevs = mcl_get_ndev();

        let mut dev = mcl_device_info {
            id: 0,
            name: [0;256],
            vendor: [0;256],
            type_: 0,
            status: 0,
            mem_size: 0,
            pes: 0,
            ndims: 0,
            wgsize: 0,
            wisize: null_mut(),
        };
        println!("Found {} devives", ndevs);

        for i in 0..ndevs {
            assert_eq!(mcl_get_dev(i, &mut dev), 0);
            // let dev: mcl_device_info = *dev;
            println!("Device {}: vendor: {:?} type: {} status: {} PEs: {} mem: {}",
            dev.id, dev.vendor, dev.type_, dev.status, dev.pes, dev.mem_size);
        }

        assert_eq!(mcl_finit(), 0);
    }
}