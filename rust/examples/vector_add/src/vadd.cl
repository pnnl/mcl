__kernel void VADD (__global int* out, __global int* x, __global int* y)
{
	const int i = get_global_id(0);

	out[i] = x[i] + y[i];
}
