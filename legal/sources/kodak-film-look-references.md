# Kodak Film-Look References

## Record

- Relationship: Design Influence
- Status: Historical; the UVSR-authored LUT simulations are archived and not shipped
- Confidence: Confirmed
- Upstream: Kodak product descriptions for [VISION Color Print Film 2383/3383](https://www.kodak.com/en/motion/product/post/print-films/vision-color-2383-3383/), [Portra 400](https://kodakprofessional.com/photographers/film/color/kodak-professional-portra-400-film/516), and [Ektar 100](https://www.kodakprofessional.com/photographers/film/color/filme-kodak-professional-ektar-100/530)
- Revision: Product pages consulted for qualitative characteristics; no page snapshot was pinned
- Governing Terms: Manufacturer page copyright and trademark rights; no Kodak code, measured profile, or official LUT was incorporated

## UVSR Relationship

UVSR historically generated original AgX-working-space look simulations from
qualitative product descriptions. The archived notice says they were not
measurements, official LUTs, or exact emulations. Kodak, Portra, Ektar, and
Vision names were used descriptively, with a non-affiliation statement.

## Evidence

- [Tonemapper And LUT Postmortem](../../docs/postmortem/tonemapper-drawer-and-luts-v1.md)
- Historical notice: `git show 177176a:assets/luts/kodak/NOTICE.md`
- LUTs introduced at `177176a990fa9996a5da0bd8e638df32152ea9cd` and removed at `104530e1164c60998146e6d16801c742a2dbc341`

## Commercial Clearance

No Kodak asset or code is identified as incorporated. If the simulations are
restored, retain the descriptive-use and non-endorsement notice, review product
names and presentation for trademark risk, and avoid calling the looks official
or colorimetrically measured.
