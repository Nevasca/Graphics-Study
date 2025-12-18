// We should setup/organize constant buffers accordingly to update frequency
// cbPerObject is updated per render item, while cbPass, per render pass 
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};

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