#ifndef UVSR_RADIAL_VISIBILITY_MASK_HLSLI
#define UVSR_RADIAL_VISIBILITY_MASK_HLSLI

// One bit represents one equal-measure sector under the active estimator. A
// set bit is already occluded/claimed; a clear bit remains visible.
static const uint RadialVisibilitySectorCount = 32u;
static const uint RadialVisibilityFullMask = 0xffffffffu;

struct VisibilityInterval
{
    float minimumAngle01;
    float maximumAngle01;
};

struct RadialVisibilityMask
{
    uint occludedBits;
};

VisibilityInterval MakeVisibilityInterval(
    float minimumAngle01,
    float maximumAngle01)
{
    VisibilityInterval interval;
    interval.minimumAngle01 = minimumAngle01;
    interval.maximumAngle01 = maximumAngle01;
    return interval;
}

RadialVisibilityMask MakeEmptyRadialVisibilityMask()
{
    RadialVisibilityMask mask;
    mask.occludedBits = 0u;
    return mask;
}

uint RadialVisibilityMakeLowBitMask(uint bitCount)
{
    if (bitCount == 0u)
        return 0u;
    // Shifting a 32-bit value by 32 is undefined.
    if (bitCount >= RadialVisibilitySectorCount)
        return RadialVisibilityFullMask;
    return (1u << bitCount) - 1u;
}

// Shift both endpoints by one coherent sub-sector phase. An interval below one
// sector then contributes with probability proportional to its measure rather
// than being deterministically discarded.
uint MakeStochasticSectorRangeMask(
    VisibilityInterval interval,
    float sectorPhase)
{
    if (!isfinite(interval.minimumAngle01) ||
        !isfinite(interval.maximumAngle01) ||
        !isfinite(sectorPhase))
    {
        return 0u;
    }

    float minimum = saturate(min(
        interval.minimumAngle01, interval.maximumAngle01));
    float maximum = saturate(max(
        interval.minimumAngle01, interval.maximumAngle01));
    if (!(maximum > minimum))
        return 0u;

    float phase = frac(sectorPhase);
    uint firstSector = (uint)clamp(
        floor(minimum * float(RadialVisibilitySectorCount) + phase),
        0.0f, float(RadialVisibilitySectorCount));
    uint endSector = (uint)clamp(
        floor(maximum * float(RadialVisibilitySectorCount) + phase),
        0.0f, float(RadialVisibilitySectorCount));
    if (endSector <= firstSector)
        return 0u;

    return RadialVisibilityMakeLowBitMask(
        endSector - firstSector) << firstSector;
}

uint GetNewlyCoveredBits(uint candidateBits, uint existingBits)
{
    return candidateBits & ~existingBits;
}

uint AccumulateOccluder(
    inout RadialVisibilityMask mask,
    uint candidateBits)
{
    uint newlyCoveredBits =
        GetNewlyCoveredBits(candidateBits, mask.occludedBits);
    mask.occludedBits |= candidateBits;
    return newlyCoveredBits;
}

uint CountOccludedSectors(RadialVisibilityMask mask)
{
    return countbits(mask.occludedBits);
}

float GetSliceVisibility(RadialVisibilityMask mask)
{
    return 1.0f - float(CountOccludedSectors(mask)) /
        float(RadialVisibilitySectorCount);
}

#endif // UVSR_RADIAL_VISIBILITY_MASK_HLSLI
