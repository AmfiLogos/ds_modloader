// dsmath.h - deterministic math shared by the Mac and Windows mods (cross-play).
// musl 1.2.5 libm (MIT, see musl/COPYRIGHT), symbols prefixed dsm_, built with -ffp-contract=off and the
// non-FMA code paths, so arm64 and x64 give the same bits. Only exact IEEE operations (+ - * / sqrt, fabs,
// floor, scalbn) come from the platform; those are identical everywhere.
#pragma once
float dsm_atan2f(float y, float x); float dsm_atanf(float); float dsm_acosf(float); float dsm_asinf(float);
float dsm_sinf(float); float dsm_cosf(float); float dsm_tanf(float); void dsm_sincosf(float, float *, float *);
float dsm_expf(float); float dsm_logf(float); float dsm_log10f(float); float dsm_powf(float, float);
float dsm_sinhf(float); float dsm_coshf(float); float dsm_tanhf(float); float dsm_hypotf(float, float);
double dsm_atan2(double, double); double dsm_atan(double); double dsm_acos(double); double dsm_asin(double);
double dsm_sin(double); double dsm_cos(double); double dsm_tan(double); void dsm_sincos(double, double *, double *);
double dsm_exp(double); double dsm_log(double); double dsm_log10(double); double dsm_pow(double, double);
double dsm_sinh(double); double dsm_cosh(double); double dsm_tanh(double); double dsm_hypot(double, double);
#define DSMATH_VERSION "musl-1.2.5-nofma-1"
