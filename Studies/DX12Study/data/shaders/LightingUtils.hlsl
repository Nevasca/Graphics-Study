#define MAX_LIGHTS 16

struct Light
{
    float3 Strength; // Light color
    float FalloffStart; // Point/Spot light only
    float3 Direction; // Directional/Spot light only
    float FalloffEnd; // Point/Spot light only
    float3 Position; // Point light only
    float SpotPower; // Spot light only
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    
    // Shininess is inverse of roughness: Shininess = 1 - roughness
    float Shininess;
};

// Implements a linear attenuation factor, for point and spot lights
float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // Linear falloff
    // saturate clamps the argument to range [0,1]
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// Schlick gives an approximation to Fresnel reflectance
float3 SchlickFresnel(float3 r0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    
    float f0 = 1.f - cosIncidentAngle;
    
    float3 reflectPercent = r0 + (1.f - r0) * (f0 * f0 * f0 * f0);
    
    return reflectPercent;
}

float GetToonNdotl(float ndotl)
{
    if(ndotl > 0.5f)
    {
        return 1.f;
    }
    
    if(ndotl > 0.f)
    {
        return 0.6f;
    }
    
    return 0.4f;
}

float GetToonRoughness(float roughnessFactor)
{
    if (roughnessFactor > 0.8f)
    {
        return 0.8f;
    }
    
    if (roughnessFactor > 0.1f)
    {
        return 0.5f;
    }
    
    return 0.f;
}

// Compute ammout of light reflected into the eye; sum of diffuse reflectance and specular reflectance
float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    // Derive m from the shininess, which is derived from the roughness
    const float m = mat.Shininess * 256.f;
    
    float3 halfVec = normalize(toEye + lightVec);
    
    float roughnessFactor = (m + 8.f) * pow(max(dot(halfVec, normal), 0.f), m) / 8.f;
#if TOON_SHADING
    roughnessFactor = GetToonRoughness(roughnessFactor);
#endif
    
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);
    
    float3 specAlbedo = fresnelFactor * roughnessFactor;
    // Our spec formula goes outside [0,1], but we are doing LDR (low dynamic-range) rendering. So scale it down a bit
    specAlbedo = specAlbedo / (specAlbedo + 1.f);
    
    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;
}

float3 ComputeDirectionalLight(Light l, Material mat, float3 normal, float3 toEye)
{
    // The light vector aims opposite the direction the light ray travels
    float3 lightVec = -l.Direction;
    
    // Scale light down by Lambert's cosine law
    float ndotl = max(dot(lightVec, normal), 0.f);
#if TOON_SHADING
    ndotl = GetToonNdotl(ndotl);
#endif
    float3 lightStrength = l.Strength * ndotl;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float3 ComputePointLight(Light l, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // Vector from the surface to the light
    float3 lightVec = l.Position - pos;
    
    // Distance from surface to light
    float d = length(lightVec);
    
    if (d > l.FalloffEnd)
    {
        return 0.f;
    }
    
    // Normalize the light vector
    lightVec /= d;
    
    // Scale light down by Lambert's cosine law
    float ndotl = max(dot(lightVec, normal), 0.f);
#if TOON_SHADING
    ndotl = GetToonNdotl(ndotl);
#endif
    float3 lightStrength = l.Strength * ndotl;
    
    // Attenuate light by distance
    float att = CalcAttenuation(d, l.FalloffStart, l.FalloffEnd);
    lightStrength *= att;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float3 ComputeSpotLight(Light l, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // Vector from the surface to the light
    float3 lightVec = l.Position - pos;
    
    // Distance from surface to light
    float d = length(lightVec);
    
    if (d > l.FalloffEnd)
    {
        return 0.f;
    }
    
    // Normalize the light vector
    lightVec /= d;
    
    // Scale light down by Lambert's cosine law
    float ndotl = max(dot(lightVec, normal), 0.f);
#if TOON_SHADING
    ndotl = GetToonNdotl(ndotl);
#endif
    float3 lightStrength = l.Strength * ndotl;
    
    // Attenuate light by distance
    float att = CalcAttenuation(d, l.FalloffStart, l.FalloffEnd);
    lightStrength *= att;
    
    // Scale by spotlight
    float spotFactor = pow(max(dot(-lightVec, l.Direction), 0.f), l.SpotPower);
    lightStrength *= spotFactor;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float4 ComputeLighting(Light gLights[MAX_LIGHTS], Material mat, float3 pos, float3 normal, float3 toEye, float3 shadowFactor)
{
    float3 result = 0.f;
    
    int i = 0;
    
#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; i++)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif
    
#if (NUM_POINT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS; i < NUM_POINT_LIGHTS; i++)
    {
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
    }
#endif
    
#if (NUM_SPOT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; i++)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif
    
    return float4(result, 0.f);
}