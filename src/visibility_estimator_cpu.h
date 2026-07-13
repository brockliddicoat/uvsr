#pragma once

// ShaderMake's dependency scanner follows includes without evaluating
// __cplusplus. Keep CPU-only dependencies in this adapter so the executable
// estimator equations remain one scanner-safe C++/HLSL source file.
#include "radial_visibility_mask.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "visibility_estimator_shared.h"
