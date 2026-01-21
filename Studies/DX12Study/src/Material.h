#pragma once
#include <MathHelper.h>
#include <string>

#include "Constants.h"

namespace Studies
{
    struct Material
    {
        std::string Name{};
        
        // Index into constant buffer corresponding to this material
        int MaterialCbIndex{-1};
        
        // Index into SRV heap for diffuse texture
        int DiffuseSrvHeapIndex{-1};
        
        // Dirty flag indicating the material has changed and we need to update the constant buffer
        int NumFramesDirty = Constants::NUM_FRAME_RESOURCES;
        
        // Material constant buffer data used for shading
        DirectX::XMFLOAT4 DiffuseAlbedo = {1.f, 1.f, 1.f, 1.f};
        DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
        float Roughness = 0.25f;
        DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
    };
}
