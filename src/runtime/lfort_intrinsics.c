#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>

struct _lfortran_complex {
    float re, im;
};

#if _WIN32
typedef struct _lfortran_complex float_complex_t;
typedef struct _lfortran_complex double_complex_t;
#else
typedef float _Complex float_complex_t;
typedef double _Complex double_complex_t;
#endif


double _lfort_sum(int n, double *v)
{
    int i, r;
    r = 0;
    for (i=0; i < n; i++) {
        r += v[i];
    }
    return r;
}

void _lfort_random_number(int n, double *v)
{
    int i;
    for (i=0; i < n; i++) {
        v[i] = rand() / (double) RAND_MAX;
    }
}

void _lfortran_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    fflush(stdout);
    va_end(args);
}

void _lfortran_complex_add(struct _lfortran_complex* a,
        struct _lfortran_complex* b, struct _lfortran_complex *result)
{
    result->re = a->re + b->re;
    result->im = a->im + b->im;
}

void _lfortran_complex_sub(struct _lfortran_complex* a,
        struct _lfortran_complex* b, struct _lfortran_complex *result)
{
    result->re = a->re - b->re;
    result->im = a->im - b->im;
}

void _lfortran_complex_mul(struct _lfortran_complex* a,
        struct _lfortran_complex* b, struct _lfortran_complex *result)
{
    float p = a->re, q = a->im;
    float r = b->re, s = b->im;
    result->re = (p*r - q*s);
    result->im = (p*s + q*r);
}

void _lfortran_complex_div(struct _lfortran_complex* a,
        struct _lfortran_complex* b, struct _lfortran_complex *result)
{
    float p = a->re, q = a->im;
    float r = b->re, s = -(b->im);
    float mod_b = r*r + s*s;
    result->re = (p*r - q*s)/mod_b;
    result->im = (p*s + q*r)/mod_b;
}

void _lfortran_complex_pow(struct _lfortran_complex* a,
        struct _lfortran_complex* b, struct _lfortran_complex *result)
{
    float complex ca = CMPLXF(a->re, a->im);
    float complex cb = CMPLXF(b->re, b->im);
    float complex cr = cpowf(ca, cb);
    result->re = crealf(cr);
    result->im = cimagf(cr);
}

void _lfortran_sin(float x, float *result)
{
    *result = sin(x);
}

float _lfortran_ssin(float x)
{
    return sin(x);
}

double _lfortran_dsin(double x)
{
    return sin(x);
}

float_complex_t _lfortran_csin(float_complex_t x)
{
#ifdef _WIN32
    float_complex_t r;
    return r; // TODO: implement in MSVC
#else
    return sin(x);
#endif
}

double_complex_t _lfortran_zsin(double_complex_t x)
{
#ifdef _WIN32
    double_complex_t r;
    return r; // TODO: implement in MSVC
#else
    return sin(x);
#endif
}
