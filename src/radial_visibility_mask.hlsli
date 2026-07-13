#ifndef UVSR_RADIAL_VISIBILITY_MASK_HLSLI
#define UVSR_RADIAL_VISIBILITY_MASK_HLSLI

// One bit represents one uniformly sized angular sector of a hemisphere slice.
// A set bit is already occluded/claimed; a clear bit is currently unoccluded.
static const uint RadialVisibilitySectorCount = 32u;
static const uint RadialVisibilityFullMask = 0xffffffffu;

// These values intentionally match uvsr::SectorHitCriterion on the CPU.
static const uint SectorHitCriterion_Round = 0u;
static const uint SectorHitCriterion_Ceil = 1u;
static const uint SectorHitCriterion_Floor = 2u;

struct VisibilityInterval
{
    float minimumAngle01;
    float maximumAngle01;
};

struct RadialVisibilityMask
{
    uint occludedBits;
};

VisibilityInterval MakeVisibilityInterval(float minimumAngle01, float maximumAngle01)
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

    // Handle a complete mask explicitly: shifting a 32-bit value by 32 is
    // undefined and some shader compilers do not reliably mask the count.
    if (bitCount >= RadialVisibilitySectorCount)
        return RadialVisibilityFullMask;

    return (1u << bitCount) - 1u;
}

uint MakeSectorRangeMask(
    float minimumAngle01,
    float maximumAngle01,
    uint criterion)
{
    // Reject the whole interval rather than allowing NaN/Inf conversion to an
    // implementation-defined uint. This also makes bad reconstruction fail
    // open (unoccluded) instead of producing an arbitrary dark mask.
    if (!isfinite(minimumAngle01) || !isfinite(maximumAngle01))
        return 0u;

    float minimum = saturate(minimumAngle01);
    float maximum = saturate(maximumAngle01);

    if (maximum < minimum)
    {
        float temporary = minimum;
        minimum = maximum;
        maximum = temporary;
    }

    float span = maximum - minimum;
    if (!(span > 0.0f))
        return 0u;

    float scaledMinimum = minimum * float(RadialVisibilitySectorCount);
    float scaledMaximum = maximum * float(RadialVisibilitySectorCount);
    float firstSectorValue;
    float endSectorValue;

    if (criterion == SectorHitCriterion_Ceil)
    {
        // Claim every sector with positive angular overlap.
        firstSectorValue = floor(scaledMinimum);
        endSectorValue = ceil(scaledMaximum);
    }
    else if (criterion == SectorHitCriterion_Floor)
    {
        // Claim only sectors completely contained by the interval.
        firstSectorValue = ceil(scaledMinimum);
        endSectorValue = floor(scaledMaximum);
    }
    else
    {
        // Paper default: a sector is hit at half or greater coverage. The span
        // guard is required when both endpoints lie within the same sector.
        if (span * float(RadialVisibilitySectorCount) < 0.5f)
            return 0u;
        firstSectorValue = ceil(scaledMinimum - 0.5f);
        endSectorValue = floor(scaledMaximum + 0.5f);
    }

    uint firstSector = (uint)clamp(
        firstSectorValue, 0.0f, float(RadialVisibilitySectorCount));
    uint endSector = (uint)clamp(
        endSectorValue, 0.0f, float(RadialVisibilitySectorCount));
    if (endSector <= firstSector)
        return 0u;

    uint lowBits = RadialVisibilityMakeLowBitMask(endSector - firstSector);
    return lowBits << firstSector;
}

uint MakeSectorRangeMask(float minimumAngle01, float maximumAngle01)
{
    return MakeSectorRangeMask(
        minimumAngle01,
        maximumAngle01,
        SectorHitCriterion_Round);
}

uint MakeSectorRangeMask(VisibilityInterval interval, uint criterion)
{
    return MakeSectorRangeMask(
        interval.minimumAngle01,
        interval.maximumAngle01,
        criterion);
}

uint MakeSectorRangeMask(VisibilityInterval interval)
{
    return MakeSectorRangeMask(interval, SectorHitCriterion_Round);
}

uint GetNewlyCoveredBits(uint candidateBits, uint existingBits)
{
    return candidateBits & ~existingBits;
}

uint AccumulateOccluder(inout RadialVisibilityMask mask, uint candidateBits)
{
    uint newlyCoveredBits = GetNewlyCoveredBits(candidateBits, mask.occludedBits);
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
