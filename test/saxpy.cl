__kernel void SAXPY (const __global float* x, const __global float* y, float a, __global float* z)
{
    const int i = get_global_id (0);

    z[i] = a*x[i] + y[i];
}