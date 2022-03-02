__kernel void VADD (__global unsigned int* x, __global unsigned int* y, __global unsigned int* out)
{
	const int i = get_global_id(0);

	out[i] = x[i] + y[i];
}