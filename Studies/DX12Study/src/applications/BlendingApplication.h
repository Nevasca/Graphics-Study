#pragma once
#include <vector>
#include <Waves.h>

#include "Application.h"
#include "FrameResource.h"
#include "Material.h"
#include "RenderItem.h"
#include "MeshGeometry.h"
#include "Vertex.h"
#include "Texture.h"

namespace Studies
{
    class BlendingApplication : public Application
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
        float m_ZoomSensitivity{0.3f};
        float m_MaxZoomRadius{160.f};

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
        UINT m_PassCbvOffset{0};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CbvDescriptorHeap{};
        Microsoft::WRL::ComPtr<ID3DBlob> m_VertexShaderBytecode{nullptr};
        Microsoft::WRL::ComPtr<ID3DBlob> m_PixelShaderBytecode{nullptr};
        std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputElementDescriptions{};
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects{};
        bool m_IsWireframe{false};
        
        std::vector<std::unique_ptr<UploadBuffer<Vertex>>> m_WaveVerticesFrameResources;
        std::unique_ptr<Waves> m_Waves;
        RenderItem* m_WavesRenderItem{nullptr};
        
        std::unordered_map<std::string, std::unique_ptr<Material>> m_Materials{};
        
        float m_SunTheta{1.25f * DirectX::XM_PI};
        float m_SunPhi{DirectX::XM_PIDIV4};
        
        std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvDescriptorHeap{};
        
        void SetupTextures();
        void CreateSRVDescriptorHeap();
        void CreateSRVViews();
        std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

        void SetupMaterials();
        void SetupLandGeometry();
        void SetupWaves();
        void SetupCrate();
        void SetupRenderItems();
        void SetupShaderAndInputLayout();
        void CreatePipelineStateObjects();
        
        float GetHillsHeight(float x, float z);
        DirectX::XMFLOAT3 GetHillsNormal(float x, float z);
        DirectX::XMFLOAT4 GetHillsColor(float y);
        void UpdateWaves();

        void UpdateMaterialConstantBuffers();
        
        void UpdateSun();
        void AnimateMaterials();
    };
}
