#pragma once
#include <DirectXMath.h>
#include <MathHelper.h>

#include "Constants.h"
#include "Light.h"

namespace Studies
{
    // Constant data that is fixed over a given rendering pass
    struct PassConstants
    {
        DirectX::XMFLOAT4X4 View{MathHelper::Identity4x4()};
        DirectX::XMFLOAT4X4 InvView{MathHelper::Identity4x4()};
        DirectX::XMFLOAT4X4 Proj{MathHelper::Identity4x4()};
        DirectX::XMFLOAT4X4 InvProj{MathHelper::Identity4x4()};
        DirectX::XMFLOAT4X4 ViewProj{MathHelper::Identity4x4()};
        DirectX::XMFLOAT4X4 InvViewProj{MathHelper::Identity4x4()};
        DirectX::XMFLOAT3 EyePositionWorld{0.f, 0.f, 0.f};
        float CbPerObjectPad1{0.f};
        DirectX::XMFLOAT2 RenderTargetSize{0.f, 0.f};
        DirectX::XMFLOAT2 InvRenderTargetSize{0.f, 0.f};
        float NearZ{0.f};
        float FarZ{0.f};
        float TotalTime{0.f};
        float DeltaTime{0.f};
        
        DirectX::XMFLOAT4 AmbientLight{0.f, 0.f, 0.f, 1.f};
        
        // Indices [0, NUM_DIR_LIGHTS] are directional lights
        // Indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS + NUM_POINT_LIGHTS] are point lights
        // Indices [NUM_POINT_LIGHTS, NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS] are spot lights for a maximum of MAX_LIGHTS per object
        Light Lights[Constants::MAX_LIGHTS];
    };
}
