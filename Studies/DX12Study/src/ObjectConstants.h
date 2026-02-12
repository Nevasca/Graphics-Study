#pragma once
#include <DirectXMath.h>
#include <MathHelper.h>

namespace Studies
{
    struct ObjectConstants
    {
        DirectX::XMFLOAT4X4 World;
        DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    };
}
