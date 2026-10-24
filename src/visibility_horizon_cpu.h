#pragma once

// ShaderMake follows includes without evaluating __cplusplus. Keep the CPU
// standard-library dependencies in this adapter and the shared equations in a
// scanner-safe C++/HLSL header.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "visibility_horizon_shared.h"
