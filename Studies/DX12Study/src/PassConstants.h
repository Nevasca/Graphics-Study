#pragma once
#include <DirectXMath.h>

namespace Studies
{
    // Constant data that is fixed over a given rendering pass
    struct PassConstants
    {
        DirectX::XMFLOAT4X4 View;
        DirectX::XMFLOAT4X4 InvView;
        DirectX::XMFLOAT4X4 Proj;
        DirectX::XMFLOAT4X4 InvProj;
        DirectX::XMFLOAT4X4 ViewProj;
        DirectX::XMFLOAT4X4 InvViewProj;
        DirectX::XMFLOAT3 EyePositionWorld;
        float CbPerObjectPad1;
        DirectX::XMFLOAT2 RenderTargetSize;
        DirectX::XMFLOAT2 InvRenderTargetSize;
        float NearZ;
        float FarZ;
        float TotalTime;
        float DeltaTime;
    };
}
