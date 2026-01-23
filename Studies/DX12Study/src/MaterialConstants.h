#pragma once

#include <MathHelper.h>

namespace Studies
{
    struct MaterialConstants
    {
        DirectX::XMFLOAT4 DiffuseAlbedo = {1.f, 1.f, 1.f, 1.f};
        DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
        float Roughness = 0.25f;
        DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
    };
}
