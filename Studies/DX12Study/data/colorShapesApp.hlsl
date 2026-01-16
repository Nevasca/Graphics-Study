// We should setup/organize constant buffers accordingly to update frequency
// cbPerObject is updated per render item, while cbPass, per render pass 
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};

// Exercise 7.9.2 (using root constants for gWorld instead of descriptor table on ShapesApp)
// cbuffer cbPerObject : register(b2)
// {
//     float gWorld0;
//     float gWorld1;
//     float gWorld2;
//     float gWorld3;
//     float gWorld4;
//     float gWorld5;
//     float gWorld6;
//     float gWorld7;
//     float gWorld8;
//     float gWorld9;
//     float gWorld10;
//     float gWorld11;
//     float gWorld12;
//     float gWorld13;
//     float gWorld14;
//     float gWorld15;
// };

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePositionWorld;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
};

struct VertexIn
{
    float3 PosLocal : POSITION; 
    float4 Color : COLOR;
};

struct VertexOut
{
    float4 PosHomogeneousClip : SV_POSITION;
    float4 Color : COLOR;
};

float GetEaseInSineTime(float normalizedTime);

VertexOut VS(VertexIn vertexIn)
{
    VertexOut vertexOut;

    // Exercise 7.9.2
    // float4x4 gWorld = 
    // {
    //     gWorld0, gWorld1, gWorld2, gWorld3,
    //     gWorld4, gWorld5, gWorld6, gWorld7,
    //     gWorld8, gWorld9, gWorld10, gWorld11,
    //     gWorld12, gWorld13, gWorld14, gWorld15
    // };

    // The extra vector-matrix multiplication per vertex is negligible on modern GPUs
    float4 positionWorld = mul(float4(vertexIn.PosLocal, 1.f), gWorld);
    vertexOut.PosHomogeneousClip = mul(positionWorld, gViewProj);

    vertexOut.Color = vertexIn.Color;
    
    return vertexOut;
}

float4 PS(VertexOut pixelIn) : SV_Target
{
    return pixelIn.Color;
}