#[cfg(feature = "shared_mem")]
use fork::{fork, Fork};

#[cfg(feature = "shared_mem")]
mod shm;

#[cfg(feature = "shared_mem")]
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
