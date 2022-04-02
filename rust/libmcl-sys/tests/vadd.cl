__kernel void VADD (__global unsigned long* x, __global unsigned long* y, __global unsigned long* out)
{
	const int i = get_global_id(0);

	out[i] = x[i] + y[i];
}