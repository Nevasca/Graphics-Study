#pragma once
#include <DirectXMath.h>
#include <MathHelper.h>
#include <memory>
#include <wrl/client.h>

#include "src/Demo.h"
#include "src/MeshGeometry.h"
#include "src/UploadBuffer.h"

namespace Studies
{
    struct ObjectConstants
    {
        DirectX::XMFLOAT4X4 WorldViewProj{MathHelper::Identity4x4()};
    };
    
    namespace Demos
    {
        class BoxDemo : public Demo
        {
        public:

            void Initialize(ID3D12Device& device, ID3D12GraphicsCommandList& commandList) override;
            void Tick(float deltaTime) override;
            void Draw(ID3D12Device& device, ID3D12GraphicsCommandList& commandList) override;

            ID3D12PipelineState* GetInitialPipelineState() const override;

        private:

            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_constantBufferViewHeap{};
            std::unique_ptr<UploadBuffer<ObjectConstants>> m_objectConstantBuffer{};
            Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature{};
            Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineStateObject{};

            std::unique_ptr<MeshGeometry> m_BoxGeometry{};

            std::vector<D3D12_INPUT_ELEMENT_DESC> m_inputElementDescriptions{};
            Microsoft::WRL::ComPtr<ID3DBlob> m_vertexShaderByteCode{nullptr};
            Microsoft::WRL::ComPtr<ID3DBlob> m_pixelShaderByteCode{nullptr};

            void CreateConstantBufferViewHeap(ID3D12Device& device);
            void CreateConstantBufferView(ID3D12Device& device);
            void CreateRootSignature(ID3D12Device& device);
            void CreatePipelineStateObject(ID3D12Device& device);

            void SetupShader();
            void SetupCube(ID3D12Device& device, ID3D12GraphicsCommandList& commandList);
        };
    }
}
