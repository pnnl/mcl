# mcl-rs
This project hosts the high-level wrappers of the mcl rust bindings.

## Summary
This crate provides high-level, rust-friendly bindings for mcl. The purpose of these bindings are
to expose a user-friendlier API to what the low-level libmcl-sys API offers. It provides wrappers
for all mcl public functions and tries to provide safety at compilation type, however,
because of the nature of the library counting on a C project there are cases that it's only possible
to catch errors at runtime.


## Building mcl-rs
### Required libraries/ crates
* [libmcl-sys](https://github.com/pnnl/mcl/tree/master/rust/libmcl-sys) and its dependencies
* Other crates listed in Cargo.toml


### Instructions
mcl-rs depends on the crate libmcl-sys which provides the low-level bindings between the C library of MCL and these higher bindings. 

```libmcl-sys``` makes use of clang to generate the low-level rust binding from the ```MCL``` header file, so if clang is not available it must be installed to the system.

1. Install clang

Once all dependencies have been taken care of, we can build mcl-rs.

2. ```cargo build --release```

## Installing MCL Scheduler
The MCL scheduler can easily be installed via:
```bash
cargo install mcl_sched
```

Note, if you have manually built MCL from the C source code, you will already have the ```mcl_sched``` binary in the MCL install directory.
You are free to use either your manually built mcl_sched or the one installed via cargo

## Testing
mcl-rs comes with a set of unit tests that can be executed with:
```
cargo test <test_name>
``` 
**Reminder**: The MCL scheduler should be running when executing the tests.
if you installed mcl_sched via cargo then you should be able to invoke directly:
```bash
 mcl_sched
```
If you built mcl manually you may need to specify the path to the mcl_sched binary

Removing the test-name would cause cargo to run all available tests, however, this could create issues since tests would run in parallel causing multiple threads to try to acquire access to the mcl_scheduler shmem object at the same time which might lead to failure.


## Documentation
Use ```cargo doc --open``` to build and open the documentation of this crate.


## STATUS
MCL, libmcl-sys, and mcl-rs are research prototypes and still under development, thus not all intended features are yet implemented.

## CONTACTS
Please, contact Roberto Gioiosa at PNNL (roberto.gioiosa@pnnl.gov) if you have any MCL questions.
For Rust related questions please contact Ryan Friese at PNNL (ryan.friese@pnnl.gov)

### MCL-Rust Team
Roberto Gioiosa  
Ryan Friese   
Polykarpos Thomadakis

## LICENCSE
This project is licensed under the BSD License - see the [LICENSE](LICENSE) file for details.

## REFERENCES
IF you wish to cite MCL, please, use the following reference:

* Roberto Gioiosa, Burcu O. Mutlu, Seyong Lee, Jeffrey S. Vetter, Giulio Picierro, and Marco Cesati. 2020. The Minos Computing Library: efficient parallel programming for extremely heterogeneous systems. In Proceedings of the 13th Annual Workshop on General Purpose Processing using Graphics Processing Unit (GPGPU '20). Association for Computing Machinery, New York, NY, USA, 1–10. DOI:https://doi.org/10.1145/3366428.3380770

Other work that leverage or describe additional MCL features:

* A. V. Kamatar, R. D. Friese and R. Gioiosa, "Locality-Aware Scheduling for Scalable Heterogeneous Environments," 2020 IEEE/ACM International Workshop on Runtime and Operating Systems for Supercomputers (ROSS), 2020, pp. 50-58, doi:10.1109/ROSS51935.2020.00011.
* Rizwan Ashraf and Roberto Gioiosa, "Exploring the Use of Novel Spatial Accelerators in Scientific Applications" 2020 ACM/SPEC International Conference on Performance Engineering (ICPE), 2022.
