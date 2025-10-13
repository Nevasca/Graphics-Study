#pragma once
#include <wrl/client.h>

#include "src/Demo.h"

namespace Studies
{
    namespace Demos
    {
        class BoxDemo : public Demo
        {
        public:

            void Initialize(ID3D12Device& device, ID3D12GraphicsCommandList& commandList) override;
            void Tick(float deltaTime) override;
            void Draw(ID3D12Device& device, ID3D12GraphicsCommandList& commandList) override;

        private:

            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_constantBufferViewHeap{};
            
            Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBufferGPU;
            Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBufferUploader;
            Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBufferGPU;
            Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBufferUploader;

            void CreateConstantBufferViewHeap(ID3D12Device& device);
            
            void SetupCube(ID3D12Device& device, ID3D12GraphicsCommandList& commandList);
            void SetupVertexBuffer(ID3D12Device& device, ID3D12GraphicsCommandList& commandList);
            void SetupIndexBuffer(ID3D12Device& device, ID3D12GraphicsCommandList& commandList);
        };
    }
}
