#include <minos_sched.h>
#include <minos_sched_internal.h>

/*
We separate the scheduler internals into a library so that we can easily construct both C and Rust mcl_sched versions
*/
int main(int argc, char *argv[]) {
    return mcl_initiate_scheduler(argc, argv);
}
