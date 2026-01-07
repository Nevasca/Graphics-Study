#include "ShapesApplication.h"

#include "Constants.h"
#include "Screen.h"

namespace Studies
{
    void ShapesApplication::Initialize(HWND mainWindow, int windowWidth, int windowHeight)
    {
        assert(mainWindow);

        m_hWindow = mainWindow;
        m_ClientWidth = windowWidth;
        m_ClientHeight = windowHeight;
        
        Screen::OnResize(windowWidth, windowHeight);

#if defined(DEBUG) || defined(_DEBUG)
        EnableDebugLayer();  
#endif

        CreateDevice();
        CreateFence();
        CacheDescriptorSizes();
        Check4xMsaaQuality();
        CreateCommandQueue();
        CreateSwapChain();
        CreateRenderTargetDescriptorHeap();
        CreateDepthStencilDescriptorHeap();
        CreateRenderTargetView();
        CreateDepthStencilView();
        
        CreateFrameResources();
        CreateRootSignature();

        m_Timer.Reset();

        // Execute the initialization commands
        ThrowIfFailed(m_CommandList->Close());
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);

        FlushCommandQueue();
    }

    void ShapesApplication::Tick()
    {
        m_Timer.Tick();
        
        CalculateFrameStats();
        
        SetNextFrameResource();
        
        UpdateObjectConstantBuffers();
        UpdatePassConstantBuffer();

        Draw();
    }

    void ShapesApplication::SetNextFrameResource()
    {
        m_CurrentFrameResourceIndex = (m_CurrentFrameResourceIndex + 1) % Constants::NUM_FRAME_RESOURCES;
        m_CurrentFrameResource = m_FrameResources[m_CurrentFrameResourceIndex].get();
        
        // If GPU has not finished processing commands of the current frame, we need to wait
        if(m_CurrentFrameResource->Fence != 0 && m_Fence->GetCompletedValue() < m_CurrentFrameResource->Fence)
        {
            HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
            ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFrameResource->Fence, eventHandle));
            WaitForSingleObject(eventHandle, INFINITE);
            CloseHandle(eventHandle);
        }
    }

    void ShapesApplication::Draw()
    {
        // TODO: build and submit command list for this frame
        
        m_CurrentFence++;
        m_CurrentFrameResource->Fence = m_CurrentFence;
        
        m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence);
        
        // GPU could still be working on commands from previous frames, but it's okay as we are not
        // touching any frame resources associated with those frames
    }
    
    void ShapesApplication::UpdateObjectConstantBuffers()
    {
        UploadBuffer<ObjectConstants>* currentObjectConstantBuffer = m_CurrentFrameResource->ObjectConstantBuffer.get();
        
        for(std::unique_ptr<RenderItem>& renderItem : m_AllRenderItems)
        {
            // Only update the constant buffer if the constants have changed
            if(renderItem->NumFramesDirty <= 0)
            {
                continue;
            }
            
            DirectX::XMMATRIX worldMatrix =  DirectX::XMLoadFloat4x4(&renderItem->WorldMatrix);
            ObjectConstants objectConstants;
            DirectX::XMStoreFloat4x4(&objectConstants.World, DirectX::XMMatrixTranspose(worldMatrix));
            
            currentObjectConstantBuffer->CopyData(renderItem->ObjectConstantBufferIndex, objectConstants);
            
            // Next frame resource need to be updated too
            renderItem->NumFramesDirty--;
        }
    }

    void ShapesApplication::UpdatePassConstantBuffer()
    {
        DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&m_View);
        DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(&m_Proj);
        
        DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);

        DirectX::XMVECTOR viewDeterminant = DirectX::XMMatrixDeterminant(view);
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&viewDeterminant, view);
        
        DirectX::XMVECTOR projDeterminant = DirectX::XMMatrixDeterminant(proj);
        DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(&projDeterminant, proj);
        
        DirectX::XMVECTOR viewProjDeterminant = DirectX::XMMatrixDeterminant(viewProj);
        DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(&viewProjDeterminant, viewProj);
        
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.View, DirectX::XMMatrixTranspose(view));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.InvView, DirectX::XMMatrixTranspose(invView));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.Proj, DirectX::XMMatrixTranspose(proj));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.InvProj, DirectX::XMMatrixTranspose(invProj));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.ViewProj, DirectX::XMMatrixTranspose(viewProj));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.InvViewProj, DirectX::XMMatrixTranspose(invViewProj));

        m_MainPassConstants.EyePositionWorld = m_EyePositionWorld;

        m_MainPassConstants.RenderTargetSize = DirectX::XMFLOAT2{static_cast<float>(m_ClientWidth), static_cast<float>(m_ClientHeight)};
        m_MainPassConstants.InvRenderTargetSize = DirectX::XMFLOAT2{1.f / m_ClientWidth, 1.f / m_ClientHeight};
        
        m_MainPassConstants.NearZ = 1.f;
        m_MainPassConstants.FarZ = 1000.f;
        
        m_MainPassConstants.TotalTime = m_Timer.GetTime();
        m_MainPassConstants.DeltaTime = m_Timer.GetDeltaTime();
        
        UploadBuffer<PassConstants>* currentPassConstantBuffer = m_CurrentFrameResource->PassConstantBuffer.get();
        currentPassConstantBuffer->CopyData(0, m_MainPassConstants);
    }

    void ShapesApplication::CreateRootSignature()
    {
        // We should not go overboard with number of constant buffers in our shaders for performance reasons
        // It's recommended to keep them under five

        CD3DX12_ROOT_PARAMETER rootParameters[2];
        
        CD3DX12_DESCRIPTOR_RANGE constantBufferViewTablePerObject;
        constantBufferViewTablePerObject.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
        rootParameters[0].InitAsDescriptorTable(1, &constantBufferViewTablePerObject);
        
        CD3DX12_DESCRIPTOR_RANGE constantBufferViewTablePass;
        constantBufferViewTablePass.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
        rootParameters[1].InitAsDescriptorTable(1, &constantBufferViewTablePass);
        
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc
        {
            2,
            rootParameters,
            0,
            nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        };
        
        Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature{nullptr};
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob{nullptr};

        HRESULT hr = D3D12SerializeRootSignature(
            &rootSignatureDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            serializedRootSignature.GetAddressOf(),
            errorBlob.GetAddressOf());

        ThrowIfFailed(hr);
        
        ThrowIfFailed(m_Device->CreateRootSignature(
            0,
            serializedRootSignature->GetBufferPointer(),
            serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS(&m_RootSignature)));
    }

    void ShapesApplication::CreateFrameResources()
    {
        UINT objectCount = static_cast<UINT>(m_AllRenderItems.size());

        for(int i = 0; i < Constants::NUM_FRAME_RESOURCES; i++)
        {
            m_FrameResources.emplace_back(std::make_unique<FrameResource>(*m_Device.Get(), 1, objectCount));
        }
    }
}
