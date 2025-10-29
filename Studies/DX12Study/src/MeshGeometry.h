#pragma once
#include <d3d12.h>
#include <d3dcommon.h>
#include <DirectXCollision.h>
#include <string>
#include <unordered_map>
#include <wrl/client.h>

namespace Studies
{
    struct SubmeshGeometry
    {
        UINT IndexCount{0};
        UINT StartIndexLocation{0};
        INT BaseVertexLocation{0};

        // Used in later chapters of the book
        DirectX::BoundingBox Bounds{};
    };
    
    struct MeshGeometry
    {
        std::string Name{};

        // System memory copies, using Blobs because the vertex/index format can be generic
        // It's up to the client to cast appropriately
        Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU{nullptr};
        Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU{nullptr};

        Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU{nullptr};
        Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU{nullptr};

        Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader{nullptr};
        Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader{nullptr};

        UINT VertexByteStride{0};
        UINT VertexBufferByteSize{0};
        DXGI_FORMAT IndexFormat{DXGI_FORMAT_R16_UINT};
        UINT IndexBufferByteSize{0};

        // A MeshGeometry may store multiple geometries in one vertex/index buffer
        std::unordered_map<std::string, SubmeshGeometry> DrawArgs{};

        D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() const;
        D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() const;
    };
}
