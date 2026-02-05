#pragma once
#include <vector>

#include "Application.h"
#include "FrameResource.h"
#include "RenderItem.h"
#include "MeshGeometry.h"

namespace Studies
{
    class LitShapesApplication : public Application
    {
    public:
        void Initialize(HWND mainWindow, int windowWidth, int windowHeight) override;
        void Tick() override;
        void Draw() override;
        
    protected:
        std::vector<std::unique_ptr<FrameResource>> m_FrameResources;
        FrameResource* m_CurrentFrameResource{nullptr};
        int m_CurrentFrameResourceIndex{0};
        
        std::vector<std::unique_ptr<RenderItem>> m_AllRenderItems;
        
        // Render items divided by PSO
        std::vector<RenderItem*> m_OpaqueRenderItems;
        std::vector<RenderItem*> m_TransparentRenderItems;

        DirectX::XMFLOAT4X4 m_World{MathHelper::Identity4x4()};
        DirectX::XMFLOAT4X4 m_View{MathHelper::Identity4x4()};
        DirectX::XMFLOAT4X4 m_Proj{MathHelper::Identity4x4()};
        DirectX::XMFLOAT3 m_EyePositionWorld{};
        
        float m_Theta{1.5f * DirectX::XM_PI};
        float m_Phi{DirectX::XM_PIDIV4};
        float m_Radius{5.0f};
        POINT m_LastMousePos{0,0};

        PassConstants m_MainPassConstants{};
        
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
        
        void CreateRootSignature();
        void CreateFrameResources();
        
        void SetNextFrameResource();
        void UpdateObjectConstantBuffers();
        void UpdatePassConstantBuffer();
        void UpdateCamera();
        
        void DrawRenderItems(ID3D12GraphicsCommandList& commandList, const std::vector<RenderItem*>& renderItems);
        
    private:
        std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> m_Geometries{};
        std::unordered_map<std::string, std::unique_ptr<Material>> m_Materials{};
        UINT m_PassCbvOffset{0};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CbvDescriptorHeap{};
        Microsoft::WRL::ComPtr<ID3DBlob> m_VertexShaderBytecode{nullptr};
        Microsoft::WRL::ComPtr<ID3DBlob> m_PixelShaderBytecode{nullptr};
        std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputElementDescriptions{};
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects{};
        bool m_IsWireframe{false};
        
        float m_SunTheta{1.25f * DirectX::XM_PI};
        float m_SunPhi{DirectX::XM_PIDIV4};
        
        void SetupMaterials();
        void SetupShapeGeometry();
        void SetupRenderItems();
        void CreateDescriptorHeaps();
        void CreateConstantBufferViews();
        void SetupShaderAndInputLayout();
        void CreatePipelineStateObjects();
        
        void UpdateMaterialConstantBuffers();
        void UpdateSun();
        
        // Exercise 8.16.5
        void SetupPointLights();
    };
}
