#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

// fn main() {
//     let s : String = String::from(env!("OUT_DIR")); 
    
//     println!("{}", s);
// }
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));