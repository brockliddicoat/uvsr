#ifndef UVSR_HIERARCHICAL_DEPTH_CB_H
#define UVSR_HIERARCHICAL_DEPTH_CB_H

struct HierarchicalDepthConstants
{
    float4 projectionDepth;
    uint2 sourceSize;
    uint2 outputSize;
    uint initialize;
    uint reverseDepth;
    uint2 padding;
};

#endif
