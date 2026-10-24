Texture2D<float> t_Visibility : register(t0);

float4 main(float4 position : SV_Position) : SV_Target
{
    float visibility = t_Visibility.Load(int3(position.xy, 0));
    return float4(visibility.xxx, 1.0f);
}
