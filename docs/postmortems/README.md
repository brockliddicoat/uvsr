# lessons from UVSR delta

UVSR delta was a long graphics experiment built with substantial help from coding agents. its preserved implementation contains useful work, but it also records how plausible graphics ideas, large inherited frameworks, weak visual oracles, and unbounded agent goals can consume a great deal of time without producing a dependable product.

these postmortems turn that history into guidance for people and LLMs doing graphics programming. the lessons apply across programming languages, graphics APIs, engines, and agent systems. they are organized by failure mode rather than by date so a future contributor can find the relevant rule before repeating an experiment.

## how to read the evidence

each document separates three kinds of statement:

- **observed** means the preserved UVSR records contain direct source, test, runtime, capture, or measurement evidence.
- **inferred** means the conclusion explains several observations but was not isolated by a controlled experiment.
- **recommended** means a future-work rule derived from the evidence.

an old passing test proves only what that test asserted. an old failure is useful evidence, but it is not a permanent ban on the underlying technique. hardware, compilers, algorithms, and project needs can change. a revival should begin with a new hypothesis and a new measurement rather than with restoration of the old implementation.

## lesson index

| topic | central lesson |
| --- | --- |
| [rendering strategy](rendering-strategy.md) | compare complete pipelines at equal time and quality, and treat missing screen-space information as unknown |
| [visual verification](visual-verification.md) | make GPU and image results queryable as compact text without reducing correctness to one score |
| [architecture and dependencies](architecture-and-dependencies.md) | understand and own the contracts at every framework boundary before translating or replacing it |
| [agent workflow](agent-workflow.md) | give agents bounded goals, competence gates, stop rules, and exact evidence identities |
| [experiment design](experiment-design.md) | prove one mechanism before adding settings, variants, persistence, and UI |
| [product and runtime](product-and-runtime.md) | compilation, runtime, visual, performance, package, and release evidence are separate |
| [historical source map](source-map.md) | every preserved postmortem and supporting document maps to its retained lesson |

## repository role

this repository is intended to improve future AI-assisted graphics work, including work that uses a different language, API, renderer, or model. the new Rust project should encode the lessons in small owned contracts, bounded experiments, structured results, and explicit retirement criteria.

[AGFX](https://github.com/AmelieHeinrich/agfx) and [ShaderToHuman](https://github.com/electronicarts/ShaderToHuman) are inspirations for API clarity and test presentation. they are not direct implementation sources for this project. the project owns its behavior, test inventory, result schema, and acceptance thresholds.

all historical records remain unchanged on [`uvsr-delta-recovery`](https://github.com/brockliddicoat/uvsr/tree/uvsr-delta-recovery). use that branch as evidence and recovery material, not as live instructions or code to copy without a fresh review.
