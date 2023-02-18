use crate::low_level;

/// The list of possible program types
#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub enum PrgType {
    // None,
    /// An OpenCL source file
    Src,
    /// An IR(Intermediate Representation) file typically specific to a given architecture
    Ir,
    /// A Bitstream, typically to be loaded to an FPGA
    Bin,
    /// A Bistream, typically to be loaded to a Dataflow or CGRA architecture
    Graph,
    // MASK,
    // NORES
}

/// An abstration for container with the computational kernels we want to execute.
///
/// A program must be one of [PrgType] types.
///
/// In some cases (e.g. [PrgType::Src]), the kernels contained in a program will be compiled at runtime,
/// we provide the option to pass along additional compiler options.
///
/// Programs must be loaded into the MCL environment using the [Prog::load] function.
/// # Example
///```
/// use mcl_rs::{MclEnvBuilder,PrgType};
///
/// let mcl = MclEnvBuilder::new().num_workers(10).initialize();
///
/// mcl.create_prog("my_path",PrgType::Src)
///     .with_compile_args("-D MYDEF")
///     .load();
/// ```
pub struct Prog {
    prog_path: String,
    compile_args: String,
    program_type: PrgType,
}

impl Prog {
    /// Creates a new mcl prog from the given path
    ///
    /// ## Arguments
    ///
    /// * `prog_path` - The path to the file where the kernel resides
    ///
    /// Returns a new Prog that can be compiled
    pub(crate) fn from(prog_path: &str, prog_type: PrgType) -> Self {
        Prog {
            prog_path: prog_path.to_string(),
            compile_args: "".to_string(),
            program_type: prog_type,
        }
    }

    /// Loads the program into the current MCL environment
    ///
    /// # Example
    ///```
    /// use mcl_rs::{MclEnvBuilder,PrgType};
    ///
    /// let mcl = MclEnvBuilder::new().num_workers(10).initialize();
    /// let prog = mcl.create_prog("my_path",PrgType::Src);
    /// prog.load();
    ///
    /// // alternatively it is common to call 'load' directly on 'create_prog'
    ///  mcl.create_prog("my_path2",PrgType::Src).load();
    /// ```
    pub fn load(self) {
        low_level::prg_load(&self.prog_path, &self.compile_args, self.program_type);
    }

    /// Allows specifiy arguments to pass to the compiler when compiling the kernels within this program
    ///
    ///
    /// Returns a new CompiledTask
    ///
    /// # Example
    ///```
    /// use mcl_rs::{MclEnvBuilder,PrgType};
    ///
    /// let mcl = MclEnvBuilder::new().num_workers(10).initialize();
    /// let prog = mcl.create_prog("my_path",PrgType::Src);
    /// prog.with_compile_args("-D MYDEF").load();
    ///
    /// // alternatively it is common to create/load a prog in one line
    ///  mcl.create_prog("my_path2",PrgType::Src)
    ///     .with_compile_args("-D MYDEF")
    ///     .load();
    /// ```
    pub fn with_compile_args(mut self, compile_args: &str) -> Self {
        self.compile_args = compile_args.to_string();
        self
    }
}
