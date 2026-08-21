# Commercial Licensing

## Why Commercial Use Requires a Conversation

UVSR is public so people can learn from it, experiment with it, improve it, and
share noncommercial work with credit. The public license intentionally does not
permit someone else to commercialize UVSR without the project's participation.
Commercial terms can instead reflect the actual product,
support, risk, and value involved rather than imposing a one-size-fits-all fee.

To discuss a commercial license,
[contact the UVSR project](mailto:brockliddicoat@gmail.com).

## Rights UVSR Can Offer

A commercial agreement can cover only first-party material controlled by the
UVSR Project Steward and contribution rights validly granted to UVSR. It cannot
override or sublicense third-party code, assets, fonts, tools, patents, or
other rights. Commercial terms, pricing, support, indemnity, and permitted
distribution require a separate signed agreement; none are promised by the
public repository.

## Current Clearance Work

The repository is not presently represented as a commercially clear binary
bundle. Known issues include:

- San Miguel 2.1 is limited to research and educational use with attribution.
- The pinned Direct3D 12 Agility SDK `1.717.1-preview` carries prerelease terms
  that prohibit sharing or distributing its files, while the current build
  copies its DLLs into runtime output.
- Optional NVIDIA NRD builds are governed by the separate NVIDIA RTX SDK
  License Agreement.
- The exact user-supplied Bistro Wine GLB needs its chain of title confirmed.
- The AgX shader now preserves Benjamin Wrensch's MIT notice, but its immediate
  historical import route and the terms governing the Troy Sobotka data lineage
  still need confirmation.
- Dependency, tool, and asset notices need a complete distribution-time audit
  for the exact package being offered.

These are clearance boundaries, not accusations that upstream authors did
anything wrong. A commercial edition may remove or replace restricted
material, obtain separate permission, or require the customer to supply
independently licensed components.

The current renderer does not copy Windows-installed UI fonts. It bundles the
exact unmodified Noto Sans v2.015 Regular, SemiBold, and Bold faces under the SIL
Open Font License 1.1 and installs the complete license with the runtime. Any
distribution must continue to satisfy the OFL's notice, standalone-sale,
naming, and endorsement conditions.

Transitional build output also carries byte-identical Noto Sans copies under
historical `CodexUI` paths so the prior launcher can validate the new source.
Those aliases are Noto Sans under the same OFL, not copied Windows fonts. The
current launcher removes them, together with the temporary historical Geist OFL
notice filename, before manifesting a new Noto-only package.

The Ogg interface option uses Dear ImGui's embedded ProggyClean font under the
MIT License. New packages include Tristan Grimmer's complete copyright and MIT
notice as `licenses/ProggyClean-MIT.txt`; distributors must preserve that notice.
These permissive font terms do not resolve the independent clearance issues
listed above.

## Evaluation Process

Before approving commercial use, identify the exact files, features, delivery
format, customers, revenue model, and jurisdictions involved. UVSR can then
separate first-party rights from third-party requirements and define a fair
written agreement. Obtain qualified legal review before relying on this file
for a commercial launch.
