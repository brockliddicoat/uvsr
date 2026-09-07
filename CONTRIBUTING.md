# contributing to UVSR

keep changes focused, preserve current product behavior unless the proposal
explicitly changes it, and remove obsolete owners end to end. read
[`AGENTS.md`](AGENTS.md), the [developer map](docs/README.md), and the
[validation contract](docs/validation.md) before changing source.

## contribution scope

explain the user or engineering outcome, affected owners, and any visible,
performance, schema, package, or compatibility change. trace current and
released consumers before deleting a path. do not restore a deliberate sunset
or edit Donut. keep generated output and build trees outside Git.

use focused checks while iterating. a source handoff needs the applicable full
developer gate. a visible, runtime, launcher, or package change also needs fresh
evidence from the exact production package. documentation only changes need
heading, link, consumer, and word count checks.

## contributor agreement

once CLA enforcement is active, every commit author must be covered by the
[UVSR Contributor License Agreement](legal/licenses/UVSR-CONTRIBUTOR-LICENSE-AGREEMENT.md)
or a separately reviewed owner agreement before merge. CLA Assistant supplies
eligible signing links and reports `license/cla`. until that status is required
on `main`, maintainers verify coverage manually. contributors retain ownership
of their work while granting UVSR the rights needed for community and separate
commercial licensing.

the hosted agreement is only for adults who personally control their work. for
work owned by an employer, client, entity, or minor, [contact UVSR](mailto:brockliddicoat@gmail.com)
for a separate agreement before submitting. bots cannot sign. maintainers must
review and explicitly approve each bot identity, output license, and provenance.

## outside material

do not include or adapt outside code, prompts, generated assets, models, fonts,
papers, shader samples, or data without disclosure. the pull request must name:

- the source URL and exact revision;
- the author and copyright holder;
- the exact license and required notices;
- every affected file and whether expression, data, ideas, or a reference were
  used; and
- any commercial, redistribution, patent, trademark, or attribution limit.

mark material not intended for inclusion as `Not a Contribution`. the CLA
grants only rights the contributor controls and does not clear third party
content. preserve the [legal and provenance records](legal/README.md).

## review

use clear, respectful language. maintainers may decline a contribution for
scope, quality, provenance, security, performance, licensing, or incomplete
evidence without questioning good faith.
