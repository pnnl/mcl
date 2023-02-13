extern crate bindgen;

use autotools;
use cmake;
use std::env;
use std::path::PathBuf;
use std::process::Command;




#[cfg(feature = "docs-rs")]
fn main() {
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    let mut bindings = bindgen::Builder::default()
        // The input header we would like to generate
        // bindings for.
        .header("doc_minos.h")
        .allowlist_var("MCL_.*")
        .allowlist_type("MCL_.*")
        .allowlist_type("mcl_.*")
        .allowlist_function("mcl_.*")
        .generate()
        .expect("Unable to generate bindings");
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
} // Skip the script when the doc is building

#[cfg(not(feature = "docs-rs"))]
fn main() {
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

    let mut ocl_libpath : String;
    let mut ocl_incpath : String;
    ocl_incpath = match env::var("OCL_PATH_INC") {
        Ok(val) => val,
        Err(_e) => "".to_string(),
    };
    ocl_libpath = match env::var("OCL_PATH_LIB") {
        Ok(val) => val,
        Err(_e) => "".to_string(),
    };

    if ocl_incpath.is_empty() || ocl_libpath.is_empty() {

        ocl_libpath.clear();
        ocl_incpath.clear();
        // Find path to CUDA library and include to extact OpenCL 
        let output = Command::new("which")
        .arg("nvcc")
        .output();
        let out = String::from_utf8(output.unwrap().stdout).unwrap();

        if !out.is_empty() {

            let mut split = out.split("bin");
            ocl_libpath = split.next().expect("Could not detect CUDA libraries").to_owned();
            ocl_incpath = ocl_libpath.clone();
            ocl_libpath.push_str("lib64/");
            ocl_incpath.push_str("include/");
        }
    }

    // println!("{}",std::path::Path::new("/usr/lib64/libOpenCL.so").exists());
    // println!("{}",std::path::Path::new("/usr/include/CL").exists() );
    if ocl_incpath.is_empty() || ocl_libpath.is_empty() {
        if std::path::Path::new("/usr/lib64/libOpenCL.so").exists() {
            ocl_libpath.push_str("/usr/lib64/");
        }
        if std::path::Path::new("/usr/include/CL").exists() {
            ocl_incpath.push_str("/usr/include/");
        }
    }

    #[cfg(not(target_os="macos"))]
    if ocl_incpath.is_empty() || ocl_libpath.is_empty() || cfg!(feature="shared_mem"){
        
        // #[cfg_attr(all(cfg(not(feature="install_pocl"),cfg(not(feature="shared_mem")))))]
        #[cfg(all(not(feature="install_pocl"),not(feature="shared_mem")))]
        panic!("Build Error. Please set the paths to OpenCL: env variables OCL_PATH_INC (current={}) and OCL_PATH_LIB (current={}). 
                Alternatively, use the 'install_pocl' feature to download and build the open source POCL (http://portablecl.org/)  OpenCL implentation.
                Note, POCL is not a Rust Crate so an internet connection is required as we will pull the source using git.
                This is highly experimental, and may require a different verson of clang and LLVM than you have installed (please see the POCL website for specifics).
                It may be useful to set the LLVM_CONFIG_PATH to point to the appropriate 'llvm-config' executable",ocl_incpath,ocl_libpath);

        #[allow(unreachable_code)]
        {
            #[cfg(all(not(feature="install_pocl"),feature="shared_mem"))]
            println!("cargo:warning=The shared_mem feature is enabled, this functionality currently requires a patched version of POCL (http://portablecl.org/) to operate.
                Note, POCL is not a Rust Crate so an internet connection is required as we will pull the source using git.
                This is highly experimental, and may require a different verson of clang and LLVM that you have installed (please see the POCL website for specifics).
                It may be useful to set the LLVM_CONFIG_PATH to point to the appropriate 'llvm-config' executable. 
                To suppress this warning please add the 'install_pocl' feature flog");
            
            #[allow(unreachable_code)]
            {
                if !out_path.clone().join("lib64/static/libOpenCL.a").exists(){
                    let pocl_dst = out_path.clone().join("pocl/");
                    // Command::new("cp").args(&["-f", "mcl/scripts/get-pocl-shared-mem.sh", &pocl_dst.to_string_lossy()])
                    //     .status().unwrap();
                    if !pocl_dst.exists(){
                        std::fs::create_dir_all(&pocl_dst).unwrap();
                    }
                    else {
                        std::fs::remove_dir_all(&pocl_dst).unwrap();
                        std::fs::create_dir_all(&pocl_dst).unwrap();
                    }
                    Command::new("cp").args(&["-f", "mcl/patches/POCL-Shared-Mem.patch", &pocl_dst.to_string_lossy()])
                        .status().unwrap();
                    Command::new("git").current_dir(&pocl_dst).args(&["clone", "git@github.com:pocl/pocl.git"]).status().unwrap();
                    Command::new("git").current_dir(pocl_dst.clone().join("pocl")).args(&["checkout", "release_1_8"]).status().unwrap();
                    Command::new("git").current_dir(pocl_dst.clone().join("pocl")).args(&["apply", "../POCL-Shared-Mem.patch"]).status().unwrap();
                    let pocl_src = pocl_dst.clone().join("pocl");
                    // if !pocl_build.exists(){
                    //     std::fs::create_dir_all(&pocl_build).unwrap();
                    // }

                    let llvm_config = match env::var("LLVM_CONFIG_PATH") {
                        Ok(val) => val,
                        Err(_e) => "llvm-config".to_string(),
                    };
                    println!("cargo:warning=llvm_config {:?}",env::var("LLVM_CONFIG_PATH"));

                    let pocl = if llvm_config.is_empty(){
                        cmake::Config::new(pocl_src).configure_arg(format!("-DBUILD_SHARED_LIBS=OFF")).configure_arg(format!("-DENABLE_ICD=OFF")).build()
                    }
                    else {
                        println!("here {llvm_config}");
                        // cmake::Config::new(pocl_src).configure_arg(format!("-DWITH_LLVM_CONFIG={llvm_config}")).configure_arg(format!("-DBUILD_SHARED_LIBS=OFF")).configure_arg(format!("-DENABLE_ICD=OFF")).build()
                        cmake::Config::new(pocl_src).configure_arg(format!("-DWITH_LLVM_CONFIG={llvm_config}")).configure_arg(format!("-DSTATIC_LLVM=ON")).configure_arg(format!("-DBUILD_SHARED_LIBS=OFF")).configure_arg(format!("-DENABLE_ICD=OFF")).build()

                    };
                    ocl_incpath = pocl.clone().join("include").to_string_lossy().to_string();
                    ocl_libpath = pocl.clone().join("lib64/static").to_string_lossy().to_string();
                }
                else {
                    ocl_incpath = out_path.clone().join("include").to_string_lossy().to_string();
                    ocl_libpath = out_path.clone().join("lib64/static").to_string_lossy().to_string();    
                }
                println!("cargo:rustc-link-search=native={ocl_libpath}");
                println!("cargo:rustc-link-lib=static=OpenCL");
            }
        }
    }


    // Find path to MCL library and include
    let mut mcl_path = match env::var("MCL_PATH") {
        Ok(val) => val,
        Err(_e) => "".to_string(),
    };

    if mcl_path.is_empty() {
        let mcl_dest=out_path.clone().join("mcl_src");
        let shm_path = mcl_dest.clone().join("mcl_shared_mem_enabled");
        
        let  shm_changed = if cfg!(feature="shared_mem") {
            !shm_path.exists() // shared_mem feature enabled but not previously compiled with it
        }
        else{ // shared_mem feature disabled
            shm_path.exists() //check if previously compiled with shared_mem
        };
        // println!("cargo:warning= {} {}",shm_path.display(),shm_changed);

        if !mcl_dest.exists() || shm_changed  {
            if shm_path.exists(){
                std::fs::remove_file(shm_path.clone()).unwrap();
            }
            // println!("cargo:warning=copying mcl");
            Command::new("cp").args(&["-rf", "mcl", &mcl_dest.to_string_lossy()])
            .status().unwrap();

            //build deps
            let libatomic_ops = mcl_dest.clone().join("deps/libatomic_ops");
            let libatomic_build = autotools::Config::new( libatomic_ops)
            .reconf("-ivfWnone")
            .build();
            println!("libatomic_build {libatomic_build:?}");
            let libatomic_inc = libatomic_build.clone().join("include");
            let libatomic_lib = libatomic_build.clone().join("include");

            let uthash_inc =  mcl_dest.clone().join("deps/uthash/include");
            
            let shared_mem = if cfg!(feature="shared_mem") {
                std::fs::File::create(shm_path).unwrap();
                // "--enable-shared-memory --enable-pocl-extensions"
                ""
            }
            else {
                ""
            };

            let  llvm_libs = if cfg!(any(feature="install_pocl",feature="shared_mem")){
                let llvm_config = match env::var("LLVM_CONFIG_PATH") {
                    Ok(val) => val,
                    Err(_e) => "llvm-config".to_string(),
                };
                println!("cargo:warning=llvm_libs {llvm_config}");
                let llvm_lib = String::from_utf8(Command::new(llvm_config.clone()).args(&["--libs", "--link-shared"])
                    .output().expect("failed to execute process").stdout).unwrap().trim().to_string();
                println!("cargo:warning={llvm_lib:?}");
                let llvm_ldflags = String::from_utf8(Command::new(llvm_config.clone()).args(&["--ldflags"])
                    .output().expect("failed to execute process").stdout).unwrap().trim().to_string();
                let llvm_cppflags = String::from_utf8(Command::new(llvm_config.clone()).args(&["--cppflags"])
                    .output().expect("failed to execute process").stdout).unwrap().trim().to_string();
                let llvm_libpath = String::from_utf8(Command::new(llvm_config.clone()).args(&["--libdir"])
                    .output().expect("failed to execute process").stdout).unwrap().trim().to_string();
                println!("cargo:warning={llvm_ldflags:?}");
                println!("cargo:rustc-link-search={llvm_libpath}");
                // println!("cargo:rustc-link-lib=dylib=clang");
                println!("cargo:rustc-link-lib=dylib={}",llvm_lib.strip_prefix("-l").unwrap());
                // format!{"-lhwloc {llvm_lib} {llvm_ldflags} /home/frie869/llvm_install/lib/libclang-cpp.so -lrt -lm -ldl -pthread"} // "}
                format!{"{llvm_cppflags} {llvm_ldflags} {llvm_lib}"} // "}

            }else{
                println!("cargo:warning=no llvm config");
                "".to_string()
            };

            println!("cargo:warning=llvm_libs {llvm_libs}");

            let mcl_build = autotools::Config::new( mcl_dest)
            .reconf("-ivfWnone")
            .cflag(format!("{} -I{} -I{} -I{ocl_incpath}",shared_mem,uthash_inc.display(),libatomic_inc.display()))
            .ldflag(format!("-L{} -L{ocl_libpath} {llvm_libs}",libatomic_lib.display()))
            // .enable("debug",None)
            .insource(true)
            .build();
            mcl_path = mcl_build.clone().to_string_lossy().to_string();
            
        }
        else {
            mcl_path = mcl_dest.to_string_lossy().to_string();
        }
        
    
    }
    
    assert!(!mcl_path.is_empty(),"Build Error. MCL_PATH environmental variable is not set");

    // Rebuild if include file has changed
    println!("cargo:rerun-if-changed={}/include/minos.h", mcl_path);
    println!("cargo:rerun-if-changed={}/include/mcl/mcl_config.h", mcl_path);
    println!("cargo:rerun-if-changed={}/include/mcl_sched.h", mcl_path);
    println!("cargo:rerun-if-changed={}/lib/libmcl.a", mcl_path);
    println!("cargo:rerun-if-changed={}/lib/mcl_sched.a", mcl_path);

    for entry in glob::glob("mcl/**/*.c").expect("failed to read glob pattern"){
        match entry {
            Ok(path) =>  println!("cargo:rerun-if-changed={}",path.display()),
            Err(_) => {},
        }
    }

    for entry in glob::glob("mcl/src/**/*.h").expect("failed to read glob pattern"){
        match entry {
            Ok(path) =>  println!("cargo:rerun-if-changed={}",path.display()),
            Err(_) => {},
        }
    }

    println!("cargo:rerun-if-env-changed=MCL_PATH");
    println!("cargo:rerun-if-env-changed=OCL_PATH_INC");
    println!("cargo:rerun-if-env-changed=OCL_PATH_LIB");

    // The bindgen::Builder is the main entry point
    // to bindgen, and lets you build up options for
    // the resulting bindings.
    let mut bindings = bindgen::Builder::default()
        // The input header we would like to generate
        // bindings for.
        .header("wrapper.h")
        .clang_arg("-I".to_owned()+&mcl_path+"/include");
    
    if !ocl_incpath.is_empty() {
        bindings = bindings.clang_arg("-I".to_owned()+&ocl_incpath);
    }
        // .clang_arg("-I".to_owned()+&ocl_incpath)
        // .clang_arg("-framework OpenCL")
        // Tell cargo to invalidate the built crate whenever any of the
        // included header files changed.
       // .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        // Finish the builder and generate the bindings.
    let bindings = bindings
    .allowlist_var("MCL_.*")
    .allowlist_type("MCL_.*")
    .allowlist_type("mcl_.*")
    .allowlist_function("mcl_.*")
    .generate()
        // Unwrap the Result and panic on failure.
        .expect("Unable to generate bindings");

    // Write generated bindings to $OUT_DIR/bindings.rs
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");

    // Tell rustcc where to look for libraries to link (-l in gcc)
    println!("cargo:rustc-link-lib=dylib=mcl");
    println!("cargo:rustc-link-lib=dylib=mcl_sched");
    #[cfg(not(target_os="macos"))]
    println!("cargo:rustc-link-lib=dylib=OpenCL");
    #[cfg(target_os="macos")]
    println!("cargo:rustc-link-lib=framework=OpenCL");
    // Tell rustcc where to look for libraries to link (-L in gcc)
    println!("cargo:rustc-link-search={}/lib/", mcl_path);
    println!("{:?}",ocl_libpath);
    if !ocl_libpath.is_empty() {
        println!("cargo:rustc-link-search={}",ocl_libpath);
    }
}
