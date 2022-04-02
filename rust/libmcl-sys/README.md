# libmcl-sys
This system crate provides Rust language bindings (via the use of Bindgen) for [Minos Computing Library](https://github.com/pnnl/mcl) (MCL).

## Build requirements
- Rust
- Cargo
- Clang
- OpenCL
- MCL

## How to build (Tested on Linux)
1. Set the ```MCL_PATH``` environmental variable with the path to MCL installation directory. For example in bash use:

    ```bash
    export MCL_PATH=/path/to/mcl/install/
    ```
    Cargo will try to find the ```lib/``` and ```include/``` directories based on that.

2. Set the ```OCL_PATH_INC``` and ```OCL_PATH_LIB``` environmental variables with the path to OpenCL ```include``` and ```lib``` (or ```lib64```) directories respectively. 

    Note: this may not be needed if OpenCL is in the system directories

3. Build using Cargo

    ```bash
    cargo build --release
    ```
    This should produce an ```.rlib``` file in ```target/release/``` directory.

## How to test
libmcl-sys comes with a set of unit tests that can be executed by running:

```bash
 cargo test --release
```
**Reminder**: The MCL scheduler should be running when executing the tests.

## STATUS
MCL (and libmcl-sys) is a research prototype and still under development, thus not all intended features are yet implemented.

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