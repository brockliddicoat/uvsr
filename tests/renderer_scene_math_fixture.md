# scene math reference data

`renderer_scene_math_fixture.h` and `renderer_scene_draw_fixture.h` are fixed
test inputs and expected results from Donut commit
`bc1ea24b0486f1c00d89327fe16c0b4dd11c5937`. they replace live Donut test
dependencies. production code does not include them.

the [capture receipt](C:/Users/brock/OneDrive/Documents/uvsr/work/donut-factor-out-v4/runs/20260909-01/09-math-fixtures-v1/reference-02/receipt.json)
preserves the generator, consumed Donut source, compiler commands and executable.
it uses MSVC x64 Release, C++17, static CRT, `/fp:precise` and disabled C++
exceptions. the generator calls no candidate function. both outputs repeat
exactly; the fixture headers only normalize CRLF to LF.

- scene math retains the original 96 seeds, two authored transforms per seed,
  zero/unit/non-unit quaternions and mirrored scaling. expected values cover
  local/world affine lanes, transformed bounds, current GPU bytes and GPU bytes
  after the original two edits. previous-frame and publication assertions remain
  in the test.
- draw data retains infinite and narrow box frusta, the reverse-depth perspective
  frustum, and all 10,000 original classifications. bit `i % 8` of byte `i / 8`
  stores case `i`. the capture checks every random input against the explicit
  right-to-left argument order used by the original MSVC control. the test uses
  that explicit order rather than relying on argument evaluation order.

these are finite regression controls. preserve exact comparisons and review
fixture changes against an independent reference; candidate output must never
rewrite its own expected data. broader numerical and runtime behavior require
their separate checks.
