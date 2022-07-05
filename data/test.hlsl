
static float4 vbuffer[] = {
    {  0.0f,  0.5f, 0.0f, 1.0f },
    { -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, 0.0f, 1.0f },
};

float4 vs_main(uint vertex_id : SV_VertexID) : SV_Position{
    return vbuffer[vertex_id];
}

float4 ps_main() : SV_Target{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}