#[cfg(any(feature = "shared_mem", feature = "pocl_extensions")]
use fork::{fork, Fork};

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions")]
mod shm;

#[cfg(any(feature = "shared_mem", feature = "pocl_extensions")]
#[test]
fn shared_mem() {
    let num_elems = 256;
    let iterations = 10;

    match fork() {
        Ok(Fork::Parent(child)) => {
            shm::consumer::start(child, num_elems, iterations);
        }
        Ok(Fork::Child) => shm::producer::start(num_elems, iterations),
        Err(_) => println!("Fork failed"),
    }
}
