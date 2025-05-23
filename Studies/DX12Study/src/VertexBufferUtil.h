#pragma once
#include <d3d12.h>
#include <wrl/client.h>

namespace Studies
{
    class VertexBufferUtil
    {
    public:
        static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
            ID3D12Device* device,
            ID3D12GraphicsCommandList* commandList,
            const void* data,
            UINT64 size,
            Microsoft::WRL::ComPtr<ID3D12Resource>& outUploadBuffer);
    };
}
