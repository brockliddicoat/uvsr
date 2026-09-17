#include "checked_worklist.h"

// deliberately rejected compile-only fixture. no throwing implementation exists.
struct PotentiallyThrowingAssignment
{
    PotentiallyThrowingAssignment& operator=(const PotentiallyThrowingAssignment&);
};
PotentiallyThrowingAssignment storage[1];
uvsr::CheckedWorklist<PotentiallyThrowingAssignment> rejected{
    uvsr::ArrayView<PotentiallyThrowingAssignment>(storage) };
