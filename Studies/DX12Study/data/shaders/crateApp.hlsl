#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtils.hlsl"

// We should setup/organize constant buffers accordingly to update frequency
// cbPerObject is updated per render item, while cbPass, per render pass 

Texture2D gDiffuseMap : register(t0);
SamplerState gSamplerPointWrap : register(s0);
SamplerState gSamplerPointClamp : register(s1);
SamplerState gSamplerLinearWrap : register(s2);
SamplerState gSamplerLinearClamp : register(s3);
SamplerState gSamplerAnisoWrap : register(s4);
SamplerState gSamplerAnisoClamp : register(s5);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};

cbuffer cbMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

// Constant data that varies per material
cbuffer cbPass : register(b2)
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
    
    float4 gAmbientLight;

    // Indices [0, NUM_DIR_LIGHTS] are directional lights
    // Indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS + NUM_POINT_LIGHTS] are point lights
    // Indices [NUM_POINT_LIGHTS, NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS] are spot lights for a maximum of MAX_LIGHTS per object
    Light gLights[MAX_LIGHTS];
};

struct VertexIn
{
    float3 PosLocal : POSITION; 
    float4 Color : COLOR;
    float3 NormalLocal : NORMAL;
};

struct VertexOut
{
    float4 PosHomogeneousClip : SV_POSITION;
    float3 PosWorld : POSITION;
    float3 NormalWorld : NORMAL;
};

float GetEaseInSineTime(float normalizedTime);

VertexOut VS(VertexIn vertexIn)
{
    VertexOut vertexOut;

    // The extra vector-matrix multiplication per vertex is negligible on modern GPUs
    float4 positionWorld = mul(float4(vertexIn.PosLocal, 1.f), gWorld);
    vertexOut.PosHomogeneousClip = mul(positionWorld, gViewProj);
    
    vertexOut.PosWorld = positionWorld.xyz;
    // Assuming nonuniform scaling; otherwise, need to use inverse-transpose of world matrix
    vertexOut.NormalWorld = mul(vertexIn.NormalLocal, (float3x3)gWorld);
    
    return vertexOut;
}

float4 PS(VertexOut pixelIn) : SV_Target
{
    // Interpolating normal can unnormalize it, so renormalize it
    pixelIn.NormalWorld = normalize(pixelIn.NormalWorld);
    
    // Vector form point being lit to eye
    float3 toEyeWorld = normalize(gEyePositionWorld - pixelIn.PosWorld);
    
    // Indirect lighting
    float4 ambient = gAmbientLight * gDiffuseAlbedo;

    float shininess = 1.f - gRoughness;
    Material material = {gDiffuseAlbedo, gFresnelR0, shininess};
    float3 shadowFactor = 1.f;
    float4 directLight = ComputeLighting(gLights, material, pixelIn.PosWorld, pixelIn.NormalWorld, toEyeWorld, shadowFactor);
    
    float4 litColor = ambient + directLight;
    
    // Common convention to take alpha from diffuse material
    litColor.a = gDiffuseAlbedo.a;

    return litColor;
}