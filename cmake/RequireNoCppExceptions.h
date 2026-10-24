#pragma once

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS) || defined(__cpp_exceptions)
#error this target requires exception-disabled compilation
#endif

#if defined(_MSC_VER) && (!defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS != 0)
#error this target requires _HAS_EXCEPTIONS=0
#endif
