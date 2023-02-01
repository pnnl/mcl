# mcl_sched
This crate provides an installable wrapper for the MCL (Minos Compute Library) Scheduler 'mcl_sched'

## Summary
This is a convenience crate for building and installing the MCL (Minos Compute Library) Scheduler 'mcl_sched'.
This can be installed using
```
cargo install mcl_sched
```
The installed mcl_sched binary can be used with both C and Rust based MCL applications (although C applications will have likely already built the scheduler manually).
Once installed, the scheduler usage is the same as if you built it manually from source.

This wrapper will try to use the system default OpenCL implementation.
If not found you will be prompted to set the ```OCL_PATH_INC``` and ```OCL_PATH_LIB``` environment variables to point the appropriate OpenCL headers and libraries.
For complex installations we recommend building MCL manually.
Instructions for this can be found at [MCL](https://github.com/pnnl/mcl).



## Installing mcl_sched
### Required libraries/ crates
* [libmcl-sys](https://github.com/pnnl/mcl/tree/master/rust/libmcl-sys) and its dependencies
* Other crates listed in Cargo.toml


### Instructions
mcl_sched depends on the crate libmcl-sys which provides the low-level rust bindings for the C library of MCL 

```libmcl-sys``` makes use of clang to generate the low-level rust binding from the ```MCL``` header file, so if clang is not available it must be installed to the system.

1. Install clang

Once all dependencies have been taken care of, we can install mcl_sched.

2. ```bash
cargo install mcl_sched
```

## Running
mcl_sched comes with a set of unit tests that can be executed with:
```
mcl_sched
``` 



## STATUS
MCL, libmcl-sys, and mcl_sched are research prototypes and still under development, thus not all intended features are yet implemented.

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
