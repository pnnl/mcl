use libmcl_sys::mcl_initiate_scheduler;
use clap::{Parser, ValueEnum};


use std::ffi::CString;

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, ValueEnum)]
enum SchedClass{
    /// First in first out scheduler
    Fifo,
    Fffs,
}

impl SchedClass{
    fn as_string(&self) -> String{
        let mut out = String::from(" -s ");
        match self {
            SchedClass::Fifo => out += "fifo ",
            SchedClass::Fffs => out += "fffs ",
        }
        out
    }
}

#[derive(Copy, Clone,Debug, PartialEq, Eq, PartialOrd, Ord, ValueEnum)]
enum ResourcePolicy{
    /// first fit
    Ff,
    /// round robin
    Rr,
    Delay,
    Hybrid,
    Lws,
}

impl ResourcePolicy{
    fn as_string(&self) -> String{
        let mut out = String::from(" -r ");
        match self {
            ResourcePolicy::Ff => out += "ff ",
            ResourcePolicy::Rr => out += "rr ",
            ResourcePolicy::Delay => out += "delay ",
            ResourcePolicy::Hybrid => out += "hybrid ",
            ResourcePolicy::Lws => out += "lws ",
        }
        out
    }
}

#[derive(Copy, Clone,Debug, PartialEq, Eq, PartialOrd, Ord, ValueEnum)]
enum EvictPolicy{
    Lru,
}
impl EvictPolicy{
    fn as_string(&self) -> String{
        let mut out = String::from(" -e ");
        match self {
            EvictPolicy::Lru => out += "lru ",
        }
        out
    }
}

#[derive(Parser,Debug)]
#[command(author, version, about, long_about = None)]
struct MclSchedCli{
    #[arg(short, long, value_enum, default_value_t = SchedClass::Fifo)]
    sched_class: SchedClass,
    #[arg(short, long, value_enum, default_value = "ff")]
    res_policy: Option<ResourcePolicy>,
    #[arg(short, long, value_enum, default_value_t = EvictPolicy::Lru)]
    evict_policy: EvictPolicy,
}

impl MclSchedCli{
    fn as_c_str(&self) -> CString{
        let mut args = String::new();
        args += &self.sched_class.as_string();
        if let Some(res_policy) = self.res_policy{
            args += &res_policy.as_string();
        }
        args += &self.evict_policy.as_string();
        CString::new(args).expect("Error converting string to CString")
    } 
}


fn main() {
    let cli = MclSchedCli::parse();
    unsafe { mcl_initiate_scheduler(0,&mut cli.as_c_str().into_raw());}
}
