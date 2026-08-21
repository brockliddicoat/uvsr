# Vercel Geist Font

## Record

- Relationship: Incorporated Upstream Material and Dependency Integration
- Status: Historical; retained source-only asset
- Confidence: Confirmed
- Upstream: [Vercel Geist Font](https://github.com/vercel/geist-font)
- Revision: Checked-in `Geist-Medium.ttf`; repository history does not record an upstream commit in this record
- Governing Terms: SIL Open Font License 1.1; copyright 2024 The Geist Project Authors

## UVSR Relationship

UVSR previously used the checked-in Geist Medium face as its non-Windows UI and
header-font fallback. The current build does not select or stage the Geist font;
the asset and its adjacent license remain in the source tree as historical
material. During the launcher transition, CMake temporarily copies the genuine
Geist OFL text to `licenses/Geist-OFL-1.1.txt` because Launcher sequence 9
requires that notice filename. The current launcher removes that compatibility
copy when normalizing a new Noto package; a sequence-9 transition package may
retain it. UVSR does not claim authorship or apply the project's source license
to the font software.

## Evidence

- [Bundled Font License](../../assets/fonts/geist/LICENSE.txt)
- Repository history before the Noto Sans v2.015 font migration
- Introduction commit `df097d74a609b34979f5d5e462525e3e7f9ae7f9`

## Commercial Clearance

The OFL permits bundling and commercial distribution subject to its notice,
license, standalone-sale, naming, and endorsement conditions. Preserve the
complete OFL text with any distribution that includes the retained Geist font.
