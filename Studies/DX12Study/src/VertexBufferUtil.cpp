#include "VertexBufferUtil.h"

#include <d3dUtil.h>
#include <d3dx12.h>

namespace Studies
{
    // Static geometries, which represents most of the geometries in a game, are put in the default heap
    // for optimal performance, so only the GPU needs to read from the vertex buffer
    // However, CPU cannot write to a vertex buffer in the default heap
    // We need to create an additional upload buffer (created on the upload heap)
    // in order to initialize the default buffer with the vertex data we want
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUtil::CreateDefaultBuffer(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        const void* data,
        UINT64 size,
        Microsoft::WRL::ComPtr<ID3D12Resource>& outUploadBuffer)
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer;

        CD3DX12_HEAP_PROPERTIES defaultHeapProperties{D3D12_HEAP_TYPE_DEFAULT};
        // CD3DX12_RESOURCE_DESC::Buffer is a DX12 helper wrapper function for creating a CD3DX12_RESOURCE_DESC describing a buffer
        // Sparing us the need to provide all arguments (check definition)
        CD3DX12_RESOURCE_DESC defaultBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

        ThrowIfFailed(device->CreateCommittedResource(
            &defaultHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &defaultBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

        CD3DX12_HEAP_PROPERTIES uploadHeapProperties{D3D12_HEAP_TYPE_UPLOAD};
        CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

        // To copy CPU memory data into our default buffer, we need to create an intermediate upload heap
        ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(outUploadBuffer.GetAddressOf())));

        // Describe the data we want to copy to the default buffer resource
        D3D12_SUBRESOURCE_DATA subResourceData = {};
        subResourceData.pData = data;
        // For buffers, RowPitch and SlicePitch are both the size of data we are copying in bytes
        subResourceData.RowPitch = static_cast<LONG_PTR>(size);
        subResourceData.SlicePitch = subResourceData.RowPitch;

        // Schedule copy of the data to the default resource
        CD3DX12_RESOURCE_BARRIER commonToCopyDestTransition = CD3DX12_RESOURCE_BARRIER::Transition(
            defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST);

        commandList->ResourceBarrier(1, &commonToCopyDestTransition);

        // Helper function will copy the CPU memory into the intermediate upload heap.
        // Then using ID3D12CommandList::CopySubresourceRegion, the intermediate upload heap data will be copied to the default buffer
        UpdateSubresources<1>(
            commandList,
            defaultBuffer.Get(),
            outUploadBuffer.Get(),
            0,
            0,
            1,
            &subResourceData);

        CD3DX12_RESOURCE_BARRIER copyDestToCommonTransition = CD3DX12_RESOURCE_BARRIER::Transition(
            defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COMMON);

        commandList->ResourceBarrier(1, &copyDestToCommonTransition);

        return defaultBuffer;
    }
}
