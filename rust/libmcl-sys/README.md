# libmcl-sys
This system crate provides Rust language bindings (via the use of Bindgen) for [Minos Computing Library](https://github.com/pnnl/mcl) (MCL).
It is highly recommended that instead of using libmcl-sys directly your instead use the higer level (and safer) [mcl-rs](https://crates.io/crates/mcl-rs) crate

## Build requirements
- Rust
- Cargo
- Clang
- OpenCL
- Autotools
- MCL (either manually installed or via ```cargo install mcl_sched```)

## How to build (Tested on Linux) without previous build of MCL
0. Set the ```OCL_PATH_INC``` and ```OCL_PATH_LIB``` environmental variables with the path to OpenCL ```include``` and ```lib``` (or ```lib64```) directories respectively. 
    Note: this may not be needed if OpenCL is in the system directories
    
1. install mcl_sched: 
    ```bash
    cargo install mcl_sched
    ```

2. Build using Cargo
    ```bash
    cargo build --release
    ```

## How to build (Tested on Linux) with manually built MCL
1.  set the ```MCL_PATH``` environmental variable with the path to MCL installation directory. For example in bash use:

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
if you installed mcl_sched via cargo then you should be able to invoke directly:
```bash
 mcl_sched
```
If you built mcl manually you may need to specify the path to the mcl_sched binary

## FEATURE FLAGS
We expose three feauture flags, losely corresponding to configuration options of the underlying MCL c-library
1.  mcl_debug - enables debug logging output from the underlying c-libary
2.  shared_mem - enables interprocess host shared memory buffers
3.  pocl_extensions - enables interprocess device based shared memory buffers, requires a patched version of POCL 1.8 to have been succesfully installed (please see <https://github.com/pnnl/mcl/tree/dev#using-custom-pocl-extensions> for more information)

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