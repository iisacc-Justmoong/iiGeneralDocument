#include <algorithm>
#include <array>
#include <cmath>
#include <windows.h>

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#include <Model/Geometry.h>

#if !defined(min) || !defined(max)
#error "iiGeneralDocument public headers must preserve the consuming application's macros"
#endif
