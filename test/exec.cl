__kernel void FACT (unsigned long n, __global unsigned long* f)
{
	unsigned long c, r = 1;

	for(c=1; c <= n; c++)
		 r *= c;

	*f = r;

	return;
}