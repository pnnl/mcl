__kernel void testCL(__global int* array, ulong len)
{
    printf("%d\n", len);
    barrier(CLK_LOCAL_MEM_FENCE);
    for(int i = 0; i < len; i++)
    {
       printf("%d\n", array[i]);
    }
}