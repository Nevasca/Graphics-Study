#pragma once
#include <DirectXCollision.h>

namespace Studies
{
    struct Light
    {
        DirectX::XMFLOAT3 Strength; // Light color
        float FalloffStart; // Point/Spot light only
        DirectX::XMFLOAT3 Direction; // Directional/Spot light only
        float FalloffEnd; // Point/Spot light only
        float SpotPower; // Spot light only
    };
}
