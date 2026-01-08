#pragma once
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

namespace Studies
{
    struct Vertex
    {
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT4 Color;
        // Exercise 6.13 - 10.
        // DirectX::PackedVector::XMCOLOR Color;
    };
    
    // Exercise 6.13 - 2.
    struct VertexPositionData
    {
        DirectX::XMFLOAT3 Position;
    };
    
    struct VertexColorData
    {
        DirectX::XMFLOAT4 Color;
    };
}
