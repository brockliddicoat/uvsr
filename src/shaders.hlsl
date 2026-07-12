static const float2 kPositions[] = {
    float2(-0.72, -0.62),
    float2( 0.00,  0.72),
    float2( 0.72, -0.62)
};

static const float3 kColors[] = {
    float3(0.10, 0.70, 1.00),
    float3(0.78, 0.26, 1.00),
    float3(0.10, 1.00, 0.68)
};

void main_vs(uint vertexId : SV_VertexID, out float4 position : SV_Position, out float3 color : COLOR)
{
    position = float4(kPositions[vertexId], 0.0, 1.0);
    color = kColors[vertexId];
}

void main_ps(float4 position : SV_Position, float3 color : COLOR, out float4 outputColor : SV_Target0)
{
    const float vignette = saturate(1.15 - length(position.xy) * 0.002);
    outputColor = float4(color * vignette, 1.0);
}
