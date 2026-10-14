// dsm_rename.h - force-included into the vendored musl files: system <math.h> first, then every musl
// libm symbol gets a dsm_ prefix so it never clashes with (or silently binds to) the platform libm.
#pragma once
#include <math.h>
#include "features.h"
#undef __FP_FAST_FMA
#undef __FP_FAST_FMAF
#define __cos dsm___cos
#define __cosdf dsm___cosdf
#define __expo2 dsm___expo2
#define __expo2f dsm___expo2f
#define __math_divzero dsm___math_divzero
#define __math_divzerof dsm___math_divzerof
#define __math_invalid dsm___math_invalid
#define __math_invalidf dsm___math_invalidf
#define __math_oflow dsm___math_oflow
#define __math_oflowf dsm___math_oflowf
#define __math_uflow dsm___math_uflow
#define __math_uflowf dsm___math_uflowf
#define __math_xflow dsm___math_xflow
#define __math_xflowf dsm___math_xflowf
#define __rem_pio2 dsm___rem_pio2
#define __rem_pio2_large dsm___rem_pio2_large
#define __rem_pio2f dsm___rem_pio2f
#define __sin dsm___sin
#define __sindf dsm___sindf
#define __tan dsm___tan
#define __tandf dsm___tandf
#define acos dsm_acos
#define acosf dsm_acosf
#define asin dsm_asin
#define asinf dsm_asinf
#define atan dsm_atan
#define atan2 dsm_atan2
#define atan2f dsm_atan2f
#define atanf dsm_atanf
#define cos dsm_cos
#define cosf dsm_cosf
#define cosh dsm_cosh
#define coshf dsm_coshf
#define exp dsm_exp
#define expf dsm_expf
#define expm1 dsm_expm1
#define expm1f dsm_expm1f
#define hypot dsm_hypot
#define hypotf dsm_hypotf
#define log dsm_log
#define log10 dsm_log10
#define log10f dsm_log10f
#define logf dsm_logf
#define pow dsm_pow
#define powf dsm_powf
#define sin dsm_sin
#define sincos dsm_sincos
#define sincosf dsm_sincosf
#define sinf dsm_sinf
#define sinh dsm_sinh
#define sinhf dsm_sinhf
#define tan dsm_tan
#define tanf dsm_tanf
#define tanh dsm_tanh
#define tanhf dsm_tanhf
#define __exp_data dsm___exp_data
#define __log_data dsm___log_data
#define __pow_log_data dsm___pow_log_data
#define __exp2f_data dsm___exp2f_data
#define __logf_data dsm___logf_data
#define __powf_log2_data dsm___powf_log2_data
#include "dsmath.h"
double dsm_expm1(double); float dsm_expm1f(float);
