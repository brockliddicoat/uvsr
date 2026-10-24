#pragma once

#if defined(_CPPUNWIND) || defined(__cpp_exceptions)
#error the complete probe graph must compile without C++ exceptions
#endif
#if !defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS != 0
#error the probe graph must use one consistent standard-library exception mode
#endif
