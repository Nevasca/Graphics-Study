#pragma once
#include <d3d12.h>
#include <d3dUtil.h>
#include <wrl/client.h>

namespace Studies
{
    template<typename T>
    class UploadBuffer
    {
    public:
        UploadBuffer(ID3D12Device& device, UINT elementCount, bool isConstantBuffer)
            : m_isConstantBuffer(isConstantBuffer)
        {
            m_elementByteSize = sizeof(T);

            // Constant buffer elements need to be multiples of 256 bytes
            // This is because the hardware can only view constant data at m * 256 byte offsets and of n * 256 byte lengths
            if (isConstantBuffer)
            {
                m_elementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));
            }

            CD3DX12_HEAP_PROPERTIES heapProperties{D3D12_HEAP_TYPE_UPLOAD};
            CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_elementByteSize * elementCount);

            ThrowIfFailed(device.CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_uploadBuffer)));

            // Subresource: identify the subresource to map. For a buffer, the only subresource is the buffer itself, so we use 0
            // pReadRange: optional pointer to a D3D12_RANGE structure that describes the range of memory to map. null for mapping the entire resource
            // ppData: returns a pointer to the mapped data. We can later use memcpy on it to copy system memory to the constant buffer
            ThrowIfFailed(m_uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedData)));

            // We don't need to unmap until we are done with the resource
            // However, we must not write to the resource while it's in use by the GPU (so we must use synchronization techniques)
        }

        UploadBuffer(const UploadBuffer& rhs) = delete;
        UploadBuffer& operator=(const UploadBuffer& rhs) = delete;

        ~UploadBuffer()
        {
            if (m_uploadBuffer != nullptr)
            {
                // Subresource: same for the map explanation
                // pWrittenRange: optional pointer to a D3D12_RANGE structure describing the range of memory to unmap. null as we want to unmap the entire resource
                m_uploadBuffer->Unmap(0, nullptr);
            }

            m_mappedData = nullptr;
        }

        ID3D12Resource* GetResource() const
        {
            return m_uploadBuffer.Get();
        }

        void CopyData(int elementIndex, const T& data)
        {
            memcpy(&m_mappedData[elementIndex * m_elementByteSize], &data, sizeof(T));
        }

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadBuffer;
        BYTE* m_mappedData{nullptr};
        UINT m_elementByteSize{0};
        bool m_isConstantBuffer{false};
    };
}
