Texture2D<float> t_Debug : register(t0);

float4 main(float4 position : SV_Position) : SV_Target
{
    float value = t_Debug.Load(int3(position.xy, 0));
    return float4(value.xxx, 1.0f);
}
