float4 main(uint vertexId : SV_VertexID) : SV_Position
{
    const float2 position = float2(
        (vertexId << 1u) & 2u,
        vertexId & 2u);
    return float4(
        position * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        1.0f,
        1.0f);
}
