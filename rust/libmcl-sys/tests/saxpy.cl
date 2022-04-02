__kernel void SAXPY(__global int* x, int a, __global int* y, __global int* z)
{
    const int i = get_global_id (0);

    z[i] = a * x[i] + y[i];
}