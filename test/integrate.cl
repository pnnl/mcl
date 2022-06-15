__kernel void VADD (__global unsigned long* x, __global unsigned long* v)
{
	const int i = get_global_id(0);

	x[i] = x[i] + v[i];
}