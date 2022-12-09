extern crate bindgen;

use autotools;
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

    if ocl_incpath.is_empty() || ocl_libpath.is_empty() {
        if std::path::Path::new("/usr/lib64/libOpenCL.so").exists() {
            ocl_libpath.push_str("/usr/lib64/");
        }
        if std::path::Path::new("/usr/include/CL").exists() {
            ocl_incpath.push_str("/usr/include/");
        }
    }

    #[cfg(not(target_os="macos"))]
    if ocl_incpath.is_empty() || ocl_libpath.is_empty() {
        panic!("Build Error. Please set the paths to OpenCL: env variables OCL_PATH_INC and OCL_PATH_LIB.");
    }


    // Find path to MCL library and include
    let mut mcl_path = match env::var("MCL_PATH") {
        Ok(val) => val,
        Err(_e) => "".to_string(),
    };

    if mcl_path.is_empty() {
        let mcl_dest=out_path.clone().join("mcl_src");
        Command::new("cp").args(&["-r", "mcl", &mcl_dest.to_string_lossy()])
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

        // let nbhash = mcl_dest.clone().join("src/common/nbhashmap");
        // let _nbhash_build = cc::Build::new()
        // .file(nbhash.join("nbhashmap.c"))
        // .include(libatomic_inc.clone())
        // .flag("-std=c11")
        // .compile("nbhashmap.o");

        let mcl_build = autotools::Config::new( mcl_dest)
        .reconf("-ivfWnone")
        .cflag(format!("-I{} -I{}",uthash_inc.display(),libatomic_inc.display()))
        .ldflag(format!("-L{}",libatomic_lib.display()))
        .insource(true)
        .build();
        mcl_path = mcl_build.to_string_lossy().to_string();
    
    }
    
    assert!(!mcl_path.is_empty(),"Build Error. MCL_PATH environmental variable is not set");

    // Rebuild if include file has changed
    println!("cargo:rerun-if-changed={}/include/minos.h", mcl_path);
    println!("cargo:rerun-if-changed={}/include/mcl/mcl_config.h", mcl_path);
    println!("cargo:rerun-if-changed={}/include/mcl_sched.h", mcl_path);
    println!("cargo:rerun-if-changed={}/lib/libmcl.a", mcl_path);
    println!("cargo:rerun-if-changed={}/lib/mcl_sched.a", mcl_path);

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
