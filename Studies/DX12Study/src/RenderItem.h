#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <MathHelper.h>

#include "Constants.h"

namespace Studies
{
    struct MeshGeometry;
    // Lightweight struct stores parameters to draw a shape
    // Drawing an object requires setting multiple parameters, such as binding vertex, index buffer, object constants,
    // setting primitive type and specifying the DrawIndexed parameters.
    // We call the set of data needed to submit a full draw call a render item
    class RenderItem
    {
    public:
        RenderItem() = default;
        
        DirectX::XMFLOAT4X4 WorldMatrix{MathHelper::Identity4x4()};
        
        // Dirty flag indicating the object data has changed and we need to update the constant buffer
        // Since we have a cbuffer for each FrameResource, we have to apply the update to each one of them
        int NumFramesDirty = Constants::NUM_FRAME_RESOURCES;
        
        // Index into GPU constant buffer corresponding to the object constant buffer
        UINT ObjectConstantBufferIndex{0};
        
        // Note: multiple render items can share same geometry
        MeshGeometry* Geometry{nullptr};
        D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology{D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST};
        
        // Draw indexed parameters
        UINT IndexCount{0};
        UINT StartIndexLocation{0};
        int BaseVertexLocation{0};
    };
}
