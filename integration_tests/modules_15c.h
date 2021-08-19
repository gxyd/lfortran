#ifndef MODULES_15C
#define MODULES_15C

struct _lfortrantest_float_complex {
    float re, im;
};

struct _lfortrantest_double_complex {
    double re, im;
};

#if _WIN32
typedef struct _lfortrantest_float_complex float_complex_t;
typedef struct _lfortrantest_double_complex double_complex_t;
#else
typedef float _Complex float_complex_t;
typedef double _Complex double_complex_t;
#endif


int f_int_float(int *a, float *b);
int f_int_double(int *a, double *b);
int f_int_double_complex(int *a, double_complex_t *b);
int f_int_float_value(int a, float b);
int f_int_double_value(int a, double b);
int f_int_intarray(int n, int *b);
float f_int_floatarray(int n, float *b);
double f_int_doublearray(int n, double *b);

#endif // MODULES_15C
