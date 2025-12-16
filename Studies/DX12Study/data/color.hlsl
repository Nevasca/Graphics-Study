// HLSL = high level shading language

cbuffer cbGlobal : register(b0)
{
    float gTime;
};

cbuffer cbPerObject : register(b1)
{
    float4x4 gWorldViewProj;
    float4 gColorOverTime;
    float gColorOverTimeSpeed;
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

float GetEaseInSineTime(float normalizedTime);

// We could have also not used structs and place all parameters on the VS function and using out for output values for next stage, like so:
// void VS(float3 iPosL : POSITION, float3 iColor : COLOR, out float4 oPosH : SV_POSITION, out float4 oColor : COLOR)
// Using structs is much more organized though
VertexOut VS(VertexIn vertexIn) // VS = Vertex Shader
{
    VertexOut vertexOut;

    vertexIn.PosLocal.xy += 0.5f * sin(vertexIn.PosLocal.x) * sin(3.f * gTime);
    vertexIn.PosLocal.z += 0.6f + 0.4f * sin(2.f * gTime);

    vertexOut.PosHomogeneousClip = mul(float4(vertexIn.PosLocal, 1.f), gWorldViewProj);

    // vertexOut.Color = vertexIn.Color;
    
    // Exercise 6.13 - 14.
    float normalizedTime = sin(gTime * gColorOverTimeSpeed);
    float easeTime = GetEaseInSineTime(normalizedTime);
    
    vertexOut.Color = lerp(vertexIn.Color, gColorOverTime, easeTime);
    
    return vertexOut;
}

// Pixel shader input must match what vertex shader outputs.
// If instead of having an output struct we had separate out values, we would then need to match on PS arguments as well,
// such as float4 PS(float4 posH : SV_POSITION, float4 color : COLOR) : SV_Target
// SV_Target is the semantic indicating that the return value type of the pixel shader should match the render target format
float4 PS(VertexOut pixelIn) : SV_Target // PS = Pixel Shader
{
    // return pixelIn.Color;

    // Exercise 6.13 - 14.
    // float normalizedTime = sin(gTime * gColorOverTimeSpeed);
    // float easeTime = GetEaseInSineTime(normalizedTime);
    //
    // return lerp(pixelIn.Color, gColorOverTime, easeTime);
    
    // Exercise 6.13 - 15.
    clip(pixelIn.Color.r - 0.5f);
    return pixelIn.Color;
}

float GetEaseInSineTime(float normalizedTime)
{
    static const float PI = 3.14159265f;
    return 1.f - cos((normalizedTime * PI) / 2.f);
}