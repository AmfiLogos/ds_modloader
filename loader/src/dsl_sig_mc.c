// dsl_sig_mc.c - the Monocypher core (third_party/monocypher, unchanged), built in its own unit: its ed25519 part goes into
// dsl_sig.c (the two files have static helpers of the same name). Not exported from the loader.
#ifndef _WIN32
#pragma GCC visibility push(hidden)
#endif
#include "third_party/monocypher/monocypher.c"
#ifndef _WIN32
#pragma GCC visibility pop
#endif
