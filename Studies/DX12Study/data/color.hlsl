// HLSL = high level shading language

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
};

struct VertexIn
{
    // the attached semantics ": POSITION" and ": COLOR"
    // must match the semantic specified by the D3D12_INPUT_ELEMENT_DESC, so we can match vertex elements to shader elements
    float3 PosLocal : POSITION; // Vertex local position 
    float4 Color : COLOR;
};

struct VertexOut
{
    // SV stands for System Value. There are some special semantics that GPU needs
    // SV_POSITION holds the vertex position in homogeneous clip space. GPU needs to be aware of this value
    // to involve in other operations, such as clipping, depth testing, rasterization.
    // If we use a geometry shader, the job of outputting the homogeneous clip space position can be deferred to the geometry shader
    float4 PosHomogeneousClip : SV_POSITION;
    float4 Color : COLOR;
};

// We could have also not used structs and place all parameters on the VS function and using out for output values for next stage, like so:
// void VS(float3 iPosL : POSITION, float3 iColor : COLOR, out float4 oPosH : SV_POSITION, out float4 oColor : COLOR)
// Using structs is much more organized though
VertexOut VS(VertexIn vertexIn) // VS = Vertex Shader
{
    VertexOut vertexOut;

    vertexOut.PosHomogeneousClip = mul(float4(vertexIn.PosLocal, 1.f), gWorldViewProj);

    vertexOut.Color = vertexIn.Color;
    
    return vertexOut;
}

// Pixel shader input must match what vertex shader outputs.
// If instead of having an output struct we had separate out values, we would then need to match on PS arguments as well,
// such as float4 PS(float4 posH : SV_POSITION, float4 color : COLOR) : SV_Target
// SV_Target is the semantic indicating that the return value type of the pixel shader should match the render target format
float4 PS(VertexOut pixelIn) : SV_Target // PS = Pixel Shader
{
    return pixelIn.Color;
}