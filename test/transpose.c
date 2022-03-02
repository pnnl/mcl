#include <getopt.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <minos.h>
#include "utils.h"

#define PROFILE_ENABLED 1
#define DFT_WORKERS 1l
#define DFT_THREADS 1l

#define M(ma, i, j) *((ma).m + (ma).cols * (i) + (j))

#ifdef PROFILE_ENABLED
#define PROFILE_MS(body) ({ \
	struct timespec start, end; \
	clock_gettime(CLOCK_MONOTONIC, &start); \
	body \
	clock_gettime(CLOCK_MONOTONIC, &end); \
	timespec_diff_ms(&end, &start); \
})
#else
#define PROFILE_MS(body) ({ \
	body \
	-1; \
})
#endif

#ifdef VERBOSE
#define matrix_print_debug(m) __matrix_print(m)
#else
#define matrix_print_debug(m) ;
#endif

typedef struct matrix {
	float *m;
	int rows;
	int cols;
} matrix_t;

static long workers = DFT_WORKERS;
static long threads = DFT_THREADS;
static long rows = 0l;
static long cols = 0l;

static const struct option lopts[] = {
	{"workers", required_argument, NULL, 'w'},
	{"threads", required_argument, NULL, 't'},
	{"rows",    required_argument, NULL, 'r'},
	{"cols",    required_argument, NULL, 'c'},
	{"help",    0, NULL, 'h'},
	{NULL,      0, NULL,  0 }
};

static void print_help(const char* prog)
{
	printf("Usage: %s -r <num_rows> -c <num_cols> [options]\n"
	       "\t-r, --rows <n>      Number of matrix rows    [required]\n"
	       "\t-c, --cols <n>      Number of matrix columns [required]\n"
	       "\t-w, --workers <n>   Number of MCL workers (default = %ld)\n"
	       "\t-t, --threads <n>   Number of threads/pes (default = %ld)\n"
	       "\t-h, --help          Show help\n",
	       prog, DFT_WORKERS, DFT_THREADS
	);

	exit(EXIT_FAILURE);
}

static void parse_opts(int argc, char *argv[])
{
	int opt;
	char *end = "";

	do {
		opt = getopt_long(argc, argv, "w:t:r:c:h", lopts, NULL);

		switch (opt) {
			case 'w':
				workers = strtol(optarg, &end, 10);
				break;
			case 't':
				threads = strtol(optarg, &end, 10);
				break;
			case 'r':
				rows = strtol(optarg, &end, 10);
				break;
			case 'c':
				cols = strtol(optarg, &end, 10);
				break;
			case -1:
				break;
			case 'h':
			default:
				print_help(argv[0]);
				exit(EXIT_FAILURE);
		}

		if (*end) {
			fprintf(stderr, "Error converting '%s' as a long.\n", optarg);
			exit(EXIT_FAILURE);
		}
	} while (opt != -1);

	/* sanity checks */
	if (workers <= 0 || threads <= 0 || rows <= 0 || cols <= 0) {
		print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	printf("Parsed options: \n"
	       "\tNumber of workers = %ld\n"
	       "\tNumber of threads = %ld\n"
	       "\tNumber of rows    = %ld\n"
	       "\tNumber of cols    = %ld\n",
	       workers, threads, rows, cols);
}


static matrix_t matrix_alloc(int rows, int cols)
{
	matrix_t r;

	float *ma = malloc(rows * cols * sizeof(float));

	if (!ma) {
		fprintf(stderr, "unable to allocate memory for matrix.");
		exit(1);
	}

	r.m = ma;
	r.rows = rows;
	r.cols = cols;

	return r;
}

static void matrix_free(matrix_t *mat)
{
	free(mat->m);
	mat->m = NULL;
}

static matrix_t matrix_generate(int rows, int cols)
{
	const int64_t size = rows * cols;

	matrix_t mat = matrix_alloc(rows, cols);

	for (int64_t i = 0; i < size; ++i) {
		mat.m[i] = (float) drand48();
	}

	return mat;
}

static inline void __matrix_print(const matrix_t mat)
{
	int rows = mat.rows;
	int cols = mat.cols;

	printf("matrix @ %p\n", mat.m);

	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			printf("%.2f ", M(mat, i, j));
		}
		printf("\n");
	}

	printf("====\n");
}

static int matrix_equals(matrix_t a, matrix_t b)
{
	int64_t size;

	if (a.m == b.m) /* exactly same matrix */
		return 1;

	if (a.rows != b.rows || a.cols != b.cols)
		return 0;

	size = a.rows * a.cols;

	for (int64_t i = 0; i < size; i++) {
		if (a.m[i] != b.m[i])
			return 0;
	}

	return 1;
}

/* Perform matrix transposition (sequential version).
 * @param dstmat - destination matrix (transposed one)
 * @param srcmat - source matrix
 */
static void matrix_transpose(matrix_t dstmat, matrix_t srcmat)
{
	int rows, cols;
	if (dstmat.rows != srcmat.cols ||
	    dstmat.cols != srcmat.rows)
	{
		fprintf(stderr, "matrix dimensions differs.\n");
		return;
	}

	rows = srcmat.rows;
	cols = srcmat.cols;

	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			M(dstmat, j, i) = M(srcmat, i, j);
		}
	}
}

/* Perform matrix transposition (OpenMP version, just internal cycle is
 * parallel).
 * @param dstmat - destination matrix (transposed one)
 * @param srcmat - source matrix
 */
static void matrix_transpose_omp(matrix_t dstmat, matrix_t srcmat)
{
	int rows, cols;
	if (dstmat.rows != srcmat.cols ||
	    dstmat.cols != srcmat.rows)
	{
		fprintf(stderr, "matrix dimensions differs.\n");
		return;
	}

	rows = srcmat.rows;
	cols = srcmat.cols;

	for (int i = 0; i < rows; i++) {
		#pragma omp parallel for
		for (int j = 0; j < cols; j++) {
			M(dstmat, j, i) = M(srcmat, i, j);
		}
	}
}

/* Perform transposition of a single matrix row, following MCL task prototype
 * @param in   - pointer to a single matrix row
 * @param ilen - number of columns in a row
 * @param out  - address of destination column pointer
 * @param olen - number of rows in the destination column
 * @ret 0 - function always succeed
 */
static int matrix_transpose_row(void *in, size_t ilen, void **out, size_t *olen)
{
	float *mrow_in  = (float *) in;
	float *mcol_out = (float *) *out;
	int cols = (int) ilen;
	int rows = (int) *olen;
	
	for (int j = 0; j < cols; j++) {
		*mcol_out = mrow_in[j];
		mcol_out += rows;
	}

	return 0;
} 

/* Perform matrix transposition (MCL version, each task will take care of
 * single row).
 * @param dstmat - destination matrix (transposed one)
 * @param srcmat - source matrix
 * @see matrix_transpose_row()
 */
static void matrix_transpose_mcl(matrix_t dstmat, matrix_t srcmat)
{
	int rows, cols;
	if (dstmat.rows != srcmat.cols ||
	    dstmat.cols != srcmat.rows)
	{
		fprintf(stderr, "matrix dimensions differs.\n");
		return;
	}

	rows = srcmat.rows;
	cols = srcmat.cols;

	mcl_task *tasks = malloc(sizeof(mcl_task) * rows);
	if (!tasks) {
		fprintf(stderr, "unable to allocate mcl_task descriptors.\n");
		return;
	}

	for (int i = 0; i < rows; i++) {
		
		tasks[i].func    = matrix_transpose_row;
		tasks[i].in      = srcmat.m + i*cols; /* the row i */
		tasks[i].out     = dstmat.m + i;      /* the col i */
		tasks[i].in_len  = cols;
		tasks[i].out_len = rows;
		tasks[i].flags   = MCL_TASK_CPU;
		tasks[i].pes     = threads;

		if (!mcl_exec(&tasks[i], 0x0)) {
			fprintf(stderr, "error submitting task %d.\n", i);
			break;
		}
	}

	mcl_wait_all();

	free(tasks);
}

static inline int64_t timespec_diff_ms(const struct timespec *e,
                                       const struct timespec *s)
{
	struct timespec r = {
		.tv_sec  = e->tv_sec - s->tv_sec,
		.tv_nsec = e->tv_nsec - s->tv_nsec
	};

	if (e->tv_nsec - s->tv_nsec < 0) {
		r.tv_sec -= 1;
		r.tv_nsec += 1000000000l;
	}

	return r.tv_sec*1000 + r.tv_nsec/1000000l;
}

int main(int argc, char *argv[])
{
	int do_mcl_test = 1;
	matrix_t smat, dmat, dmat_p, dmat_m;
	int64_t seq_ms, omp_ms, mcl_ms;

	parse_opts(argc, argv);

	if (mcl_init(workers, 0x0) < 0) {
		fprintf(stderr, "MCL TEST DISABLED.\n");
		do_mcl_test = 0;
	}

	omp_set_num_threads(threads);

	printf("Parallel Matrix Transpose, OMP MAX threads: %d\n",
	       omp_get_max_threads());

	smat = matrix_generate(rows, cols);
	dmat = matrix_alloc(cols, rows);
	dmat_p = matrix_alloc(cols, rows);

	if (do_mcl_test)
		dmat_m = matrix_alloc(cols, rows);

	matrix_print_debug(smat);

	seq_ms = PROFILE_MS(
		matrix_transpose(dmat, smat);
	);

	omp_ms = PROFILE_MS(
		matrix_transpose_omp(dmat_p, smat);
	);

	if (do_mcl_test) {
		mcl_ms = PROFILE_MS(
			matrix_transpose_mcl(dmat_m, smat);
		);
	}

	/* SANITY CHECK */
	if (!matrix_equals(dmat, dmat_p)) {
		fprintf(stderr, "MATRIX MISMATCH: SEQ AND OMP DIFFERS!\n");
		goto quit;
	}

	if (do_mcl_test && !matrix_equals(dmat, dmat_m)) {
		fprintf(stderr, "MATRIX MISMATCH: SEQ AND MCL DIFFERS!\n");
		goto quit;
	}

	matrix_print_debug(dmat);

quit:
	printf("seq: %ld ms\n"
	       "omp: %ld ms (speedup: %.2fx)\n",
	       seq_ms, omp_ms, (double)seq_ms/omp_ms);

	if (do_mcl_test) {
	    printf("mcl: %ld ms (speedup: %.2fx)\n",
		       mcl_ms, (double)seq_ms/mcl_ms);
		matrix_free(&dmat_m);
	}

	matrix_free(&dmat_p);
	matrix_free(&dmat);
	matrix_free(&smat);

	return 0;
}
