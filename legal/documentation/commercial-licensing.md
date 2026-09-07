# commercial licensing

## why commercial use requires a conversation

UVSR is public so people can learn from it, experiment with it, improve it, and
share noncommercial work with credit. the public license intentionally does not
permit someone else to commercialize UVSR without the project's participation.
commercial terms can instead reflect the actual product,
support, risk, and value involved rather than imposing a one-size-fits-all fee.

to discuss a commercial license,
[contact the UVSR project](mailto:brockliddicoat@gmail.com).

## rights UVSR can offer

a commercial agreement can cover only first-party material controlled by the
UVSR Project Steward and contribution rights validly granted to UVSR. it cannot
override or sublicense third-party code, assets, fonts, tools, patents, or
other rights. commercial terms, pricing, support, indemnity, and permitted
distribution require a separate signed agreement; none are promised by the
public repository.

## current clearance work

the repository is not presently represented as a commercially clear binary
bundle. known issues include:

- San Miguel 2.1 is limited to research and educational use with attribution.
- the pinned Direct3D 12 Agility SDK `1.619.5` permits distribution of its
  listed object-code files subject to its conditions. UVSR packages only
  `D3D12Core.dll` plus the complete terms and distributable list; distributors
  must independently satisfy the significant-functionality, downstream-terms,
  indemnity, trademark, Windows-use, and licensing restrictions.
- the exact user-supplied Bistro Wine GLB needs its chain of title confirmed.
- the terms governing PBRT v4's retained San Miguel entry-camera data remain
  unconfirmed; replace that small camera selection or confirm permission before
  commercial distribution.
- Wicked Engine commit `ad283cdf10ac4989078c77fc8b02a6d8daec6699` is a pinned
  read only architectural reference. UVSR retains no Wicked Engine code, data,
  binary, or package dependency. re-audit and preserve its MIT notice if future
  work incorporates recognizable upstream material.
- the AgX shader now preserves Benjamin Wrensch's MIT notice, but its immediate
  historical import route and the terms governing the Troy Sobotka data lineage
  still need confirmation.
- dependency, tool, and asset notices need a complete distribution-time audit
  for the exact package being offered.

these are clearance boundaries, not accusations that upstream authors did
anything wrong. a commercial edition may remove or replace restricted
material, obtain separate permission, or require the customer to supply
independently licensed components.

the current renderer does not copy Windows-installed UI fonts. it bundles the
exact unmodified Noto Sans v2.015 Regular, SemiBold, and Bold faces under the SIL
open Font License 1.1 and installs the complete license with the runtime. any
distribution must continue to satisfy the OFL's notice, standalone-sale,
naming, and endorsement conditions.

the Ogg interface option uses Dear ImGui's embedded ProggyClean font under the
MIT License. new packages include Tristan Grimmer's complete copyright and MIT
notice as `bin/licenses/ProggyClean-MIT.txt`; distributors must preserve that
notice.
these permissive font terms do not resolve the independent clearance issues
listed above.

## evaluation process

before approving commercial use, identify the exact files, features, delivery
format, customers, revenue model, and jurisdictions involved. UVSR can then
separate first-party rights from third-party requirements and define a fair
written agreement. obtain qualified legal review before relying on this file
for a commercial launch.
