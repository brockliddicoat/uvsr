# contributing

work from one purpose-named task branch and open its pull request directly into `main`. do not create a second merge branch. commit coherent, verified checkpoints often enough that review and recovery remain practical, while keeping tightly coupled edits together.

before editing, read [AGENTS.md](AGENTS.md) and the relevant project document. preserve the scene assets, provenance, licenses, GitHub workflows, and repository metadata unless the task explicitly changes them.

every pull request should state the concrete result, source provenance, affected backend and shader-language cells, focused verification, deferred hardware, and known uncertainty. third-party code, translated shader logic, fixtures, and generated data require exact source revisions and controlling notices.

the project is still in its planning baseline. do not describe source inspection as a successful build, GPU execution, backend parity, or upstream acceptance.
