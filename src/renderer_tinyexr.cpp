// the pinned codec has one implementation shared by import and remaining native callers.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4018)
#endif
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
