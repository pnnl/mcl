#ifdef DOUBLE_PRECISION
#define FPTYPE double
#else
#define FPTYPE float
#endif

__kernel void gemmN(const __global FPTYPE* A,
                    const __global FPTYPE* B, int N,
                    __global FPTYPE* C) 
{
    
    // Thread identifiers
    const int globalRow = get_global_id(0); // Row ID of C (0..N)
    const int globalCol = get_global_id(1); // Col ID of C (0..N)
 
    // Compute a single element (loop over K)
    FPTYPE acc = (FPTYPE) 0.0;
    for (int k=0; k<N; k++) {
           acc += A[globalRow*N + k] * B[k*N + globalCol];
    }
    // Store the result
    C[globalRow*N + globalCol] = acc;
}