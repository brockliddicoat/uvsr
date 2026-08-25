#ifndef UVSR_SAMPLE_ACCUMULATION_HLSLI
#define UVSR_SAMPLE_ACCUMULATION_HLSLI

static const uint UVSR_SAMPLE_SEQUENCE_FRAME_PHASE = 0u;
static const uint UVSR_SAMPLE_SEQUENCE_SUCCESSFUL_COUNT = 1u;
static const uint UVSR_SAMPLE_SEQUENCE_ANIMATED_RESET = 2u;

bool UvsrSampleScheduleEnabled(uint sequenceMode)
{
    return sequenceMode != UVSR_SAMPLE_SEQUENCE_FRAME_PHASE;
}

uint UvsrResolveSampleSequencePhase(
    uint sequenceMode,
    uint attemptToken,
    uint framePhase)
{
    if (sequenceMode == UVSR_SAMPLE_SEQUENCE_ANIMATED_RESET)
        return framePhase;
    return sequenceMode == UVSR_SAMPLE_SEQUENCE_SUCCESSFUL_COUNT &&
            attemptToken > 0u
        ? attemptToken - 1u
        : framePhase;
}

#endif // UVSR_SAMPLE_ACCUMULATION_HLSLI
