# borrowed worklists

[`ArrayView`](array_view.h) and [`CheckedWorklist`](checked_worklist.h) provide only
the pointer/count and LIFO scratch operations needed by current consumers.
they have no STL, graphics, OS, ownership or allocation dependency. see the
[glossary](../docs/glossary.md).

the caller supplies named storage containing live, correctly aligned objects.
copying a view never owns or extends that storage. shape checks reject null
nonempty views, byte-count overflow and misalignment; they cannot prove pointer
accessibility. no owning-container or initializer-list conversion is provided.
the worklist retains its view until destruction, so its storage must outlive it.
elements use nonthrowing assignment, enforced at instantiation. use plain records;
the worklist neither destroys popped elements nor releases their resources.

`TryPush` checks capacity without growth. `TryPop` checks emptiness and leaves
the output unchanged on failure. grouped operations check total remaining space
before their first push. there is no spill, truncation or hidden heap fallback.

## current example

[`CameraCollisionWorld::BuildNodes`](camera_collision.cpp) borrows a local task
array during scene preparation. uint32 triangle counts and eight-triangle leaves
bound pending work to 30 records: 29 successive left descents plus one current
task. the constexpr loop derives the bound without compile-time recursion.
right is pushed before left so node creation and median partitioning retain
left-first preorder. each pop creates one node or rejects malformed internal
ranges; children are forward indices, not recursive owners.

the selected MSVC 14.44 `nth_element` implementation and its median/partition/
insertion-sort helpers are iterative and nonallocating. the C++ standard does
not promise those implementation details; re-audit a changed toolset. no bit-count
helper is needed, so no untested zero-input intrinsic is introduced.

collision's current typed-array ownership and transactional publication are
defined in the [scene module](renderer_scene.md#editing-and-camera-collision).
the worklist borrows scratch during construction and never owns the triangle or
node arrays.

EASTL is omitted: no unresolved owning-container requirement justifies another
dependency or a general container replacement here. the independently developed
small-header approach is informed by the pinned [NoGraphicsAPI helper design](https://github.com/sebbbi/NoGraphicsAPI/commit/a2efb6c28768c5775d5b6140d5723a60a72b0936),
without importing its platform restrictions or temporary-container borrowing.

## checks

[worklist tests](../tests/checked_worklist_tests.cpp) cover empty/full boundaries,
LIFO order, unchanged failed output, malformed views and the maximum-width
descent. the [negative compile fixture](../tests/portable_contracts/throwing_assignment.cpp)
must fail specifically for potentially throwing assignment.
[collision tests](../tests/camera_collision_tests.cpp) exercise the real builder,
literal preorder, forward references, exact leaf coverage and checked node
exhaustion. fixed-toolset fingerprints are evidence for comparing migrations,
not permanent promises about unstable median-partition permutations.

the [isolated probe](../tests/portable_contracts/CMakeLists.txt) applies one
exception mode throughout its graph. build it in a separate tree, not by linking
its `_HAS_EXCEPTIONS=0` objects into the remaining exception-enabled engine.
the optional importer probe consumes caller-supplied pinned fastgltf/simdjson
sources. valid input and `InvalidJson` are checked before `.get()`; this does not
prove recoverable vendor OOM or nonrecursive importer traversal.
