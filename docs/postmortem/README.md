# Experiment Postmortem Archive

This directory preserves retired UVSR experiments, the evidence that led to
their retirement, and constraints for any successor. An archived experiment is
not part of the renderer baseline and is not a recommendation to restore its
implementation wholesale.

## Records

- [Native-Resolution Analytical/Reconstructive Temporal Anti-Aliasing v1](native-resolution-analytical-reconstructive-temporal-anti-aliasing-v1.md)
  records the retired temporal anti-aliasing
  experiment and the required order for a smaller successor.
- [Visibility Sample Rotation v1](visibility-sample-rotation-v1.md) records the
  technically consistent but visually unsuccessful receiver-dithering
  experiment, its negative product evaluation, and exact revival triggers.
- [Three-Band Time-of-Day Sky v1](three-band-time-of-day-sky-v1.md) records the
  retired atmospheric sky and celestial-motion experiment. Its
  [design snapshot](three-band-time-of-day-sky-v1-design.md) preserves the final
  implementation contract as historical context, not accepted product design.

## Archive Meaning

Each record distinguishes technical checks from product acceptance. A build,
test pass, or mathematically consistent reference model can still fail visual
validation. Archive branches retain exact source for inspection and selective
extraction; they are deliberately removed from active roadmap tracking.

## Resuming or Restarting

Resume an archive only when investigating its exact historical behavior. Use
the checkpoint named by its postmortem rather than guessing from timestamps or
build folders.

Start a successor from the newest Canonical verified checkpoint available at
that future time. Read the postmortem first, reproduce the minimum rejected case,
and prove the replacement's core invariant before restoring secondary features.
