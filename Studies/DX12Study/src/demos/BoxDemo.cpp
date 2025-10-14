#include "BoxDemo.h"

#include <DirectXColors.h>
#include <wrl/client.h>

#include "src/Vertex.h"
#include "src/VertexBufferUtil.h"

#include <d3dUtil.h>

namespace Studies
{
    namespace Demos
    {
        void BoxDemo::Initialize(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            CreateConstantBufferViewHeap(device);
            CreateConstantBufferView(device);
            
            SetupCube(device, commandList);
        }

        void BoxDemo::Tick(float deltaTime)
        { }

        void BoxDemo::Draw(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            // When not using index buffers, we use ID3D12GraphicsCommandList::DrawInstanced
            // When using index buffers, we use ID3D12GraphicsCommandList::DrawIndexedInstanced
        }

        void BoxDemo::CreateConstantBufferViewHeap(ID3D12Device& device)
        {
            D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
            // Since on this BoxDemo we don't have an SRV or UAV descriptors, and we are going to render only one object, we just need 1 descriptor in the heap
            cbvHeapDesc.NumDescriptors = 1;
            cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            // D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE indicate that the descriptor will be accessed by shader programs
            cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            cbvHeapDesc.NodeMask = 0;

            ThrowIfFailed(device.CreateDescriptorHeap(
                &cbvHeapDesc,
                IID_PPV_ARGS(m_constantBufferViewHeap.GetAddressOf())));
        }

        void BoxDemo::CreateConstantBufferView(ID3D12Device& device)
        {
            m_objectConstantBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(device, 1, true);

            UINT objectConstantBufferSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

            // Address to start of the buffer (0th constant buffer)
            D3D12_GPU_VIRTUAL_ADDRESS constantBufferAddress = m_objectConstantBuffer->GetResource()->GetGPUVirtualAddress();

            // Offset to the ith object constant buffer in the buffer. In our case, we are only using one object
            int boxConstantBufferIndex = 0;
            constantBufferAddress += boxConstantBufferIndex * objectConstantBufferSize;

            // D3D12_CONSTANT_BUFFER_VIEW_DESC describes a subset of the constant buffer resource to bind to the HLSL
            D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
            constantBufferViewDesc.BufferLocation = constantBufferAddress; // Must be multiple of 256 bytes
            constantBufferViewDesc.SizeInBytes = objectConstantBufferSize; // Must be multiple of 256 bytes

            device.CreateConstantBufferView(
                &constantBufferViewDesc,
                m_constantBufferViewHeap->GetCPUDescriptorHandleForHeapStart());
        }

        void BoxDemo::SetupCube(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            SetupVertexBuffer(device, commandList);
            SetupIndexBuffer(device, commandList);
        }

        void BoxDemo::SetupVertexBuffer(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            Vertex vertices[] =
            {
                {DirectX::XMFLOAT3{-1.f, -1.f, -1.f}, DirectX::XMFLOAT4{DirectX::Colors::White}},
                {DirectX::XMFLOAT3{-1.f, 1.f, -1.f}, DirectX::XMFLOAT4{DirectX::Colors::Black}},
                {DirectX::XMFLOAT3{1.f, 1.f, -1.f}, DirectX::XMFLOAT4{DirectX::Colors::Red}},
                {DirectX::XMFLOAT3{1.f, -1.f, -1.f}, DirectX::XMFLOAT4{DirectX::Colors::Green}},
                {DirectX::XMFLOAT3{-1.f, -1.f, 1.f}, DirectX::XMFLOAT4{DirectX::Colors::Blue}},
                {DirectX::XMFLOAT3{-1.f, 1.f, 1.f}, DirectX::XMFLOAT4{DirectX::Colors::Yellow}},
                {DirectX::XMFLOAT3{1.f, 1.f, 1.f}, DirectX::XMFLOAT4{DirectX::Colors::Cyan}},
                {DirectX::XMFLOAT3{1.f, -1.f, 1.f}, DirectX::XMFLOAT4{DirectX::Colors::Magenta}},
            };

            constexpr UINT64 vertexBufferSize = 8 * sizeof(Vertex);

            m_vertexBufferGPU = nullptr;
            m_vertexBufferUploader = nullptr;

            m_vertexBufferGPU = VertexBufferUtil::CreateDefaultBuffer(&device, &commandList, vertices, vertexBufferSize, m_vertexBufferUploader);

            // In order to bind a vertex buffer to the pipeline, we need to create a vertex buffer view, which doesn't need a descriptor heap
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
            vertexBufferView.BufferLocation = m_vertexBufferGPU->GetGPUVirtualAddress();
            vertexBufferView.SizeInBytes = vertexBufferSize;
            vertexBufferView.StrideInBytes = sizeof(Vertex);

            // Bind the vertex view to an input slot of the pipeline (from 0 to 15) to feed vertices to the input assembler stage
            // A vertex buffer will stay bound to an input slot until we change it
            D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[1] = { vertexBufferView };
            commandList.IASetVertexBuffers(0, 1, vertexBufferViews);
        }

        void BoxDemo::SetupIndexBuffer(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            uint16_t indices[] =
            {
                // Front face
                0, 1, 2,
                0, 2, 3,

                // Back face
                4, 6, 5,
                4, 7, 6,

                // Left face
                4, 5, 1,
                4, 1, 0,

                // Right face
                3, 2, 6,
                3, 6, 7,

                // Top face
                1, 5, 6,
                1, 6, 2,

                // Bottom face
                4, 0, 3,
                4, 3, 7
            };

            constexpr UINT indexBufferSize = 36 * sizeof(uint16_t);

            m_indexBufferGPU = nullptr;
            m_indexBufferUploader = nullptr;

            m_indexBufferGPU = VertexBufferUtil::CreateDefaultBuffer(&device, &commandList, indices, indexBufferSize, m_indexBufferUploader);

            D3D12_INDEX_BUFFER_VIEW indexBufferView{};
            indexBufferView.BufferLocation = m_indexBufferGPU->GetGPUVirtualAddress();
            // For format, we should use DXGI_FORMAT_R16_UINT for 16-bit indices to reduce bandwidth.
            // Only use DXGI_FORMAT_R32_UINT if we have indexes that need the extra 32-bit range
            indexBufferView.Format = DXGI_FORMAT_R16_UINT;
            indexBufferView.SizeInBytes = indexBufferSize;

            commandList.IASetIndexBuffer(&indexBufferView);
        }
    }
}
