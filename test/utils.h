#ifndef MCL_TEST_UTILS_H
#define MCL_TEST_UTILS_H

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#define XSTR(x) STR(x)
#define STR(x) #x

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

#define TEST_SUCCESS "\x1B[32mSUCCESS \x1B[0m"
#define TEST_FAILED   "\x1B[31mFAILED \x1B[0m"

#define BILLION 1000000000ULL
#define tdiff(end,start) BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec

#define DFT_WORKERS  1UL
#define DFT_SIZE     16UL
#ifdef __TEST_MCL
#define DFT_TYPE     2UL
#else
#define DFT_TYPE     0UL
#endif
#define DFT_REP      1UL
#define DFT_SYNC     0UL
#define DFT_VERIFY   0UL
#define DFT_DID      0UL
#define BASE         100
#define TOLERANCE    0.05f

#ifdef DOUBLE_PRECISION
#define FPTYPE double
typedef struct cplxdbl {
    double x;
    double y;
} CMPTYPE;
#else
#define FPTYPE float
typedef struct cplxflt {
    float x;
    float y;
} CMPTYPE;
#endif

extern uint64_t workers ;
extern size_t   size;
extern uint64_t type;    
extern uint64_t synct;   
extern uint64_t rep;     
extern uint64_t verify;  
extern uint64_t did;     
extern uint64_t flags;   

extern const struct option global_lopts[];

void mcl_banner(char*);
void mcl_verify(int);
int  mcl_load(char*, char**);
int  ocl_setup(cl_device_id**, cl_context*, cl_command_queue*, unsigned long, unsigned long);
void parse_global_opts(int, char**);
void print_global_help(char*);
#endif
