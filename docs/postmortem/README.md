# Experiment Postmortem Archive

This directory preserves retired UVSR experiments and strategically sunset
features, the decisions that removed them from active development, and
constraints for any successor. An archived item is not part of the renderer
baseline and is not automatically a recommendation to restore its
implementation wholesale.

## Records

- [Native-Resolution Analytical/Reconstructive Temporal Anti-Aliasing v1](native-resolution-analytical-reconstructive-temporal-anti-aliasing-v1.md)
  records the retired temporal anti-aliasing
  experiment and the required order for a smaller successor.
- [Tonemapper Drawer and LUTs v1](tonemapper-drawer-and-luts-v1.md) records a
  successful but prematurely timed optional feature, its exact restoration
  bundle, and the requirement to pair any revival with bilateral-grid local
  tone mapping.
- [Visibility Sample Rotation v1](visibility-sample-rotation-v1.md) records the
  technically consistent but visually unsuccessful receiver-dithering
  experiment, its negative product evaluation, and exact revival triggers.
- [Three-Band Time-of-Day Sky v1](three-band-time-of-day-sky-v1.md) records the
  retired atmospheric sky and celestial-motion experiment. Its
  [design snapshot](three-band-time-of-day-sky-v1-design.md) preserves the final
  implementation contract as historical context, not accepted product design.

## Archive Meaning

Each record distinguishes technical checks from product acceptance and product
sequencing. A build, test pass, or mathematically consistent reference model can
still fail visual validation. A sound feature can also be sunset when its
optional surface area arrives before the systems it depends on are stable.
Archive branches or restoration bundles retain exact source for inspection and
selective extraction; they are deliberately removed from active roadmap
tracking.

## Resuming or Restarting

Resume an archive only when investigating its exact historical behavior. Use
the checkpoint named by its postmortem rather than guessing from timestamps or
build folders.

Start a successor from the newest Canonical verified checkpoint available at
that future time. Read the postmortem first, reproduce the minimum rejected case,
and prove the replacement's core invariant before restoring secondary features.
Strategic-sunset records can define a different explicit revival contract; when
they do, follow that record instead of treating the archive as a failed
experiment.
