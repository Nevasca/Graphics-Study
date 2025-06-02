#include "BoxDemo.h"

#include <DirectXColors.h>
#include <wrl/client.h>

#include "src/Vertex.h"
#include "src/VertexBufferUtil.h"

namespace Studies
{
    namespace Demos
    {
        void BoxDemo::Initialize(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            SetupCube(device, commandList);
        }

        void BoxDemo::Tick(float deltaTime)
        { }

        void BoxDemo::Draw(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        { }

        void BoxDemo::SetupCube(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
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
            D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[1] = { vertexBufferView };
            commandList.IASetVertexBuffers(0, 1, vertexBufferViews);
        }
    }
}
