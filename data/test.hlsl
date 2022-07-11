
struct Vertex {
    float3 pos;
    float3 norm;
    float2 uv;
};

StructuredBuffer<Vertex> vbuffer : register(t0, space0);
StructuredBuffer<uint> ibuffer : register(t1, space0);

cbuffer Camera : register(b0, space0) {
    matrix vp;
};

struct VSOut {
    float4 sv_pos : SV_Position;
    float3 norm : Normal;
};

VSOut vs_main(uint vertex_id : SV_VertexID) {
    Vertex vertex = vbuffer[ibuffer[vertex_id]];

    VSOut vso;
    vso.sv_pos = mul(vp, float4(vertex.pos, 1.0f));
    vso.norm = vertex.norm;// TODO: normalize this when it is actually normal data

    return vso;
}

float4 ps_main(VSOut vso) : SV_Target{
    return float4(sqrt(vso.norm), 1.0f);
}