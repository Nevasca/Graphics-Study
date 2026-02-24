#include "BlendingApplication.h"

#include <GeometryGenerator.h>

#include "Constants.h"
#include "Input.h"
#include "Screen.h"
#include "ShaderUtil.h"
#include "Vector2.h"
#include "Vertex.h"
#include "VertexBufferUtil.h"

namespace Studies
{
    void BlendingApplication::Initialize(HWND mainWindow, int windowWidth, int windowHeight)
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

        SetupTextures();
        CreateSRVDescriptorHeap();
        CreateSRVViews();

        SetupMaterials();
        SetupLandGeometry();
        SetupWaves();
        SetupCrate();
        SetupRenderItems();
        CreateFrameResources();
        
        CreateRootSignature();
        SetupShaderAndInputLayout();
        CreatePipelineStateObjects();

        m_Timer.Reset();

        // Execute the initialization commands
        ThrowIfFailed(m_CommandList->Close());
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);

        FlushCommandQueue();
        
        m_Radius = m_MaxZoomRadius / 2.f;
        m_SunTheta = 5.6f;
        m_SunPhi = 1.57f;
    }

    void BlendingApplication::Tick()
    {
        m_Timer.Tick();
        
        CalculateFrameStats();
        
        SetNextFrameResource();
        
        UpdateCamera();
        UpdateSun();
        AnimateMaterials();

        UpdateObjectConstantBuffers();
        UpdatePassConstantBuffer();
        UpdateMaterialConstantBuffers();
        UpdateWaves();
        
        m_IsWireframe = Input::GetKeyboardKey('1'); 

        Draw();
    }

    void BlendingApplication::SetNextFrameResource()
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

    void BlendingApplication::Draw()
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdListAllocator = m_CurrentFrameResource->CommandListAllocator;
        
        ThrowIfFailed(cmdListAllocator->Reset());
        
        if(m_IsWireframe)
        {
            ThrowIfFailed(m_CommandList->Reset(cmdListAllocator.Get(), m_PipelineStateObjects["opaque_wireframe"].Get()));
        }
        else
        {
            ThrowIfFailed(m_CommandList->Reset(cmdListAllocator.Get(), m_PipelineStateObjects["opaque"].Get()));
        }
        
        ResizeViewport();
        ResizeScissors();
        
        CD3DX12_RESOURCE_BARRIER backBufferTransitionToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
            GetCurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        
        m_CommandList->ResourceBarrier(1, &backBufferTransitionToRenderTarget);
        
        m_CommandList->ClearRenderTargetView(
            GetCurrentBackBufferView(),
            &m_MainPassConstants.gFogColor.x,
            0, nullptr);
        
        m_CommandList->ClearDepthStencilView(
            GetCurrentDepthStencilView(),
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            1.0f, 0, 0, nullptr);
        
        D3D12_CPU_DESCRIPTOR_HANDLE currentBackBufferView = GetCurrentBackBufferView();
        D3D12_CPU_DESCRIPTOR_HANDLE currentDepthStencilView = GetCurrentDepthStencilView();
        
        m_CommandList->OMSetRenderTargets(
            1,
            &currentBackBufferView,
            true,
            &currentDepthStencilView);
        
        ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvDescriptorHeap.Get() };
        m_CommandList->SetDescriptorHeaps(1, descriptorHeaps);

        m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

        ID3D12Resource* passConstantBuffer = m_CurrentFrameResource->PassConstantBuffer->GetResource();
        m_CommandList->SetGraphicsRootConstantBufferView(3, passConstantBuffer->GetGPUVirtualAddress());
        
        DrawRenderItems(*m_CommandList.Get(), m_OpaqueRenderItems);
        
        m_CommandList->SetPipelineState(m_PipelineStateObjects["alphaTested"].Get());
        DrawRenderItems(*m_CommandList.Get(), m_AlphaTestedRenderItems);

        // For performance, we should only enable blending when needed
        m_CommandList->SetPipelineState(m_PipelineStateObjects["transparent"].Get());
        DrawRenderItems(*m_CommandList.Get(), m_TransparentRenderItems);
        
        CD3DX12_RESOURCE_BARRIER backBufferTransitionToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
            GetCurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        
        m_CommandList->ResourceBarrier(1, &backBufferTransitionToPresent);
        
        ThrowIfFailed(m_CommandList->Close());
        
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);
        
        ThrowIfFailed(m_SwapChain->Present(0, 0));
        m_CurrentBackBufferIndex = (m_CurrentBackBufferIndex + 1) % SWAPCHAIN_BUFFER_COUNT;

        m_CurrentFence++;
        m_CurrentFrameResource->Fence = m_CurrentFence;
        
        m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence);
        
        // GPU could still be working on commands from previous frames, but it's okay as we are not
        // touching any frame resources associated with those frames
    }

    void BlendingApplication::DrawRenderItems(ID3D12GraphicsCommandList& commandList, const std::vector<RenderItem*>& renderItems)
    {
        UINT objectConstantBufferByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
        UINT materialConstantBufferByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

        ID3D12Resource* objectConstantBuffer = m_CurrentFrameResource->ObjectConstantBuffer->GetResource();
        ID3D12Resource* materialConstantBuffer = m_CurrentFrameResource->MaterialConstantBuffer->GetResource();
        
        for(size_t i = 0; i < renderItems.size(); i++)
        {
            RenderItem* renderItem = renderItems[i];

            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = renderItem->Geometry->GetVertexBufferView();
            commandList.IASetVertexBuffers(0, 1, &vertexBufferView);
            
            D3D12_INDEX_BUFFER_VIEW indexBufferView = renderItem->Geometry->GetIndexBufferView();
            commandList.IASetIndexBuffer(&indexBufferView);
            
            commandList.IASetPrimitiveTopology(renderItem->PrimitiveTopology);

            CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseTextureHandle{m_SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()};
            diffuseTextureHandle.Offset(renderItem->Material->DiffuseSrvHeapIndex, m_CbvSrvDescriptorSize);
            commandList.SetGraphicsRootDescriptorTable(0, diffuseTextureHandle);
            
            D3D12_GPU_VIRTUAL_ADDRESS objectConstantBufferAddress = objectConstantBuffer->GetGPUVirtualAddress();
            objectConstantBufferAddress += renderItem->ObjectConstantBufferIndex * objectConstantBufferByteSize;
            commandList.SetGraphicsRootConstantBufferView(1, objectConstantBufferAddress);
            
            D3D12_GPU_VIRTUAL_ADDRESS materialConstantBufferAddress = materialConstantBuffer->GetGPUVirtualAddress();
            materialConstantBufferAddress += renderItem->Material->MaterialCbIndex * materialConstantBufferByteSize;
            commandList.SetGraphicsRootConstantBufferView(2, materialConstantBufferAddress);
            
            commandList.DrawIndexedInstanced(
                renderItem->IndexCount,
                1,
                renderItem->StartIndexLocation,
                renderItem->BaseVertexLocation,
                0);
        }
    }

    void BlendingApplication::UpdateObjectConstantBuffers()
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
            DirectX::XMMATRIX texTransform = DirectX::XMLoadFloat4x4(&renderItem->TexTransform);

            ObjectConstants objectConstants;
            DirectX::XMStoreFloat4x4(&objectConstants.World, DirectX::XMMatrixTranspose(worldMatrix));
            DirectX::XMStoreFloat4x4(&objectConstants.TexTransform, DirectX::XMMatrixTranspose(texTransform));

            currentObjectConstantBuffer->CopyData(renderItem->ObjectConstantBufferIndex, objectConstants);
            
            // Next frame resource need to be updated too
            renderItem->NumFramesDirty--;
        }
    }

    void BlendingApplication::UpdatePassConstantBuffer()
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
        
        m_MainPassConstants.AmbientLight = DirectX::XMFLOAT4{0.08f, 0.14f, 0.17f, 1.f};
        
        DirectX::XMVECTOR lightDirection = MathHelper::SphericalToCartesian(1.f, m_SunTheta, m_SunPhi);
        DirectX::XMStoreFloat3(&m_MainPassConstants.Lights[0].Direction, lightDirection);
        m_MainPassConstants.Lights[0].Strength = DirectX::XMFLOAT3{0.8f, 0.8f, 0.7f};

        // Exercise 8.16.1
        // constexpr float pulseSpeed = 10.f;
        // float pulseFactor = 0.5f + sinf(pulseSpeed * m_Timer.GetTime()) / 2.f;
        // m_MainPassConstants.Lights[0].Strength = DirectX::XMFLOAT3{1.0f * pulseFactor, 0.0f, 0.0f};
        
        UploadBuffer<PassConstants>* currentPassConstantBuffer = m_CurrentFrameResource->PassConstantBuffer.get();
        currentPassConstantBuffer->CopyData(0, m_MainPassConstants);
    }

    void BlendingApplication::UpdateMaterialConstantBuffers()
    {
        UploadBuffer<MaterialConstants>* currentMaterialConstantBuffer = m_CurrentFrameResource->MaterialConstantBuffer.get();

        for (const auto & materialPair : m_Materials)
        {
            Material* material = materialPair.second.get();
            
            if (material->NumFramesDirty <= 0)
            {
                continue;
            }
            
            MaterialConstants materialConstants{};
            materialConstants.DiffuseAlbedo = material->DiffuseAlbedo;
            materialConstants.FresnelR0 = material->FresnelR0;
            materialConstants.Roughness = material->Roughness;

            DirectX::XMMATRIX matTransform = DirectX::XMLoadFloat4x4(&material->MatTransform);
            DirectX::XMStoreFloat4x4(&materialConstants.MatTransform, DirectX::XMMatrixTranspose(matTransform));

            currentMaterialConstantBuffer->CopyData(material->MaterialCbIndex, materialConstants);
            
            material->NumFramesDirty--;
        }
    }

    void BlendingApplication::UpdateCamera()
    {
        Vector2 currentMousePosition = Input::GetMousePosition();

        if(Input::GetMouseButton(Input::MouseButton::Left))
        {
            // Make each pixel correspond to a quarter of a degree
            float dx = DirectX::XMConvertToRadians(0.25f * (currentMousePosition.X - static_cast<float>(m_LastMousePos.x)));
            float dy = DirectX::XMConvertToRadians(0.25f * (currentMousePosition.Y - static_cast<float>(m_LastMousePos.y)));

            // Update angles to orbit camera around box
            m_Theta += dx;
            m_Phi -= dy;

            // Restrict the angle
            m_Phi = MathHelper::Clamp(m_Phi, 0.1f, MathHelper::Pi - 0.1f);
        }
        else if(Input::GetMouseButton(Input::MouseButton::Right))
        {
            float dx = m_ZoomSensitivity * (currentMousePosition.X - static_cast<float>(m_LastMousePos.x));
            float dy = m_ZoomSensitivity * (currentMousePosition.Y - static_cast<float>(m_LastMousePos.y));

            m_Radius += dx - dy;
            m_Radius = MathHelper::Clamp(m_Radius, 3.0f, m_MaxZoomRadius);
        }

        m_LastMousePos.x = static_cast<long>(currentMousePosition.X);
        m_LastMousePos.y = static_cast<long>(currentMousePosition.Y);
        
        // Convert Spherical to Cartesian coordinates
        float x = m_Radius * sinf(m_Phi) * cosf(m_Theta);
        float z = m_Radius * sinf(m_Phi) * sinf(m_Theta) * sinf(m_Theta);
        float y = m_Radius * cosf(m_Phi);

        DirectX::XMVECTOR pos = DirectX::XMVectorSet(x, y, z, 1.f);
        m_EyePositionWorld = DirectX::XMFLOAT3{x, y, z};

        DirectX::XMVECTOR target = DirectX::XMVectorZero();
        DirectX::XMVECTOR up = DirectX::XMVectorSet(0.f, 1.f, 0.f, 0.f);

        DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
        DirectX::XMStoreFloat4x4(&m_View, view);

        DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, Screen::GetAspectRatio(), 1.f, 1000.f);
        DirectX::XMStoreFloat4x4(&m_Proj, proj);
    }

    void BlendingApplication::UpdateSun()
    {
        const float deltaTime = m_Timer.GetDeltaTime();
        
        if (Input::GetKeyboardKey(Input::KeyLeft))
        {
            m_SunTheta -= 1.f * deltaTime;
        }
        
        if (Input::GetKeyboardKey(Input::KeyRight))
        {
            m_SunTheta += 1.f * deltaTime;
        }
        
        if (Input::GetKeyboardKey(Input::KeyUp))
        {
            m_SunPhi -= 1.f * deltaTime;
        }
        
        if (Input::GetKeyboardKey(Input::KeyDown))
        {
            m_SunPhi += 1.f * deltaTime;
        }
        
        m_SunPhi = MathHelper::Clamp(m_SunPhi, 0.1f, DirectX::XM_PIDIV2);
    }

    void BlendingApplication::CreateRootSignature()
    {
        // We should not go overboard with number of constant buffers in our shaders for performance reasons
        // It's recommended to keep them under five

        CD3DX12_ROOT_PARAMETER rootParameters[4];

        D3D12_DESCRIPTOR_RANGE srvDescriptorRange{};
        srvDescriptorRange.NumDescriptors = 1;
        srvDescriptorRange.BaseShaderRegister = 0;
        srvDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rootParameters[0].InitAsDescriptorTable(1, &srvDescriptorRange);

        rootParameters[1].InitAsConstantBufferView(0); // per-object CBV
        rootParameters[2].InitAsConstantBufferView(1); //per-material CBV
        rootParameters[3].InitAsConstantBufferView(2); //per-pass CBV
        
        // If we do not use static samplers and want our own custom samplers, we would need to: 
        // 1) create a D3D12_DESCRIPTOR_RANGE with RangeType of D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
        // 2) add to the rootParameters as a descriptor table
        // 3) create a descriptor heap of type D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
        // 4) create sampler descriptors to the heap filling a D3D12_SAMPLER_DESC and calling m_Device->CreateSampler(&desc, heapHandle)
        // 5) bind the descriptor heap on draw call m_CommandList->SetDescriptorHeaps
        // 6) bind the sampler to the descriptor table commandList.SetGraphicsRootDescriptorTable
        
        // Or, we can use static samplers if we are not going to do something fancy with samplers and are not blocked by their limitations (fixed border colors, additional fields, max 2032)
        std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> staticSamplers = GetStaticSamplers();
        
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc
        {
            4,
            rootParameters,
            staticSamplers.size(),
            staticSamplers.data(),
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

    void BlendingApplication::CreateFrameResources()
    {
        UINT objectCount = static_cast<UINT>(m_AllRenderItems.size());
        UINT materialCount = static_cast<UINT>(m_Materials.size());

        for(int i = 0; i < Constants::NUM_FRAME_RESOURCES; i++)
        {
            m_FrameResources.emplace_back(std::make_unique<FrameResource>(*m_Device.Get(), 1, objectCount, materialCount));
        }
        
        // To not add to the FrameResource class an application exclusive usage, decided to create it only on this application
        for(int i = 0; i < Constants::NUM_FRAME_RESOURCES; i++)
        {
            m_WaveVerticesFrameResources.emplace_back(std::make_unique<UploadBuffer<Vertex>>(*m_Device.Get(), m_Waves->VertexCount(), false));
        }
    }

    void BlendingApplication::SetupTextures()
    {
        std::unique_ptr<Texture> woodCrateTexture = std::make_unique<Texture>();
        woodCrateTexture->Name = "WoodCrateTexture";
        woodCrateTexture->FileName = L"data//textures//WoodCrate01.dds";
        
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
            m_CommandList.Get(),
            woodCrateTexture->FileName.c_str(),
            woodCrateTexture->Resource,
            woodCrateTexture->UploadHeapResource));
        
        std::unique_ptr<Texture> grassTexture = std::make_unique<Texture>();
        grassTexture->Name = "GrassTexture";
        grassTexture->FileName = L"data//textures//Grass.dds";
        
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
            m_CommandList.Get(),
            grassTexture->FileName.c_str(),
            grassTexture->Resource,
            grassTexture->UploadHeapResource));
        
        std::unique_ptr<Texture> waterTexture = std::make_unique<Texture>();
        waterTexture->Name = "WaterTexture";
        waterTexture->FileName = L"data//textures//Water.dds";
        
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
            m_CommandList.Get(),
            waterTexture->FileName.c_str(),
            waterTexture->Resource,
            waterTexture->UploadHeapResource));
        
        std::unique_ptr<Texture> wireFence = std::make_unique<Texture>();
        wireFence->Name = "WireFence";
        wireFence->FileName = L"data//textures//WireFence.dds";
        
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
            m_CommandList.Get(),
            wireFence->FileName.c_str(),
            wireFence->Resource,
            wireFence->UploadHeapResource));

        m_Textures["woodCrate"] = std::move(woodCrateTexture);
        m_Textures["grass"] = std::move(grassTexture);
        m_Textures["water"] = std::move(waterTexture);
        m_Textures["wireFence"] = std::move(wireFence);
    }

    void BlendingApplication::CreateSRVDescriptorHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
        srvHeapDesc.NumDescriptors = static_cast<UINT>(m_Textures.size());
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        
        ThrowIfFailed(m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(m_SrvDescriptorHeap.GetAddressOf())));
    }

    void BlendingApplication::CreateSRVViews()
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE handle{m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()};

        for (const auto& element : m_Textures)
        {
            Texture* texture = element.second.get();
            
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            // When we sample a texture in shader, it returns a vector with texture data. In case we wanted to reorder the vector components, such as swapping red and green components,
            // we could do that with the Shader4ComponentMapping var. As we don't want the reordering, we specify default 
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            // How GPU should interpret data. If we created the resource with typeless format, we must specify a non-typeless format here
            srvDesc.Format = texture->Resource->GetDesc().Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = texture->Resource->GetDesc().MipLevels;
            // Minimum mipmap level that can be accessed. 0.0f means all mipmap levels can be accessed. 
            // If we set 3.0f for example, only mipmap levels from 3.0 to MipCount -1 could be accessed
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        
            m_Device->CreateShaderResourceView(texture->Resource.Get(), &srvDesc, handle);

            handle.Offset(1, m_CbvSrvDescriptorSize);
        }
    }

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> BlendingApplication::GetStaticSamplers()
    {
        const CD3DX12_STATIC_SAMPLER_DESC pointWrap{
            0,
            D3D12_FILTER_MIN_MAG_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        };
        
        const CD3DX12_STATIC_SAMPLER_DESC pointClamp{
            1,
            D3D12_FILTER_MIN_MAG_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        };
        
        const CD3DX12_STATIC_SAMPLER_DESC linearWrap{
            2,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        };
        
        const CD3DX12_STATIC_SAMPLER_DESC linearClamp{
            3,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        };
        
        const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap{
            4,
            D3D12_FILTER_ANISOTROPIC,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        };
        
        const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp{
            5,
            D3D12_FILTER_ANISOTROPIC,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        };
        
        return {pointWrap, pointClamp, linearWrap, linearClamp, anisotropicWrap, anisotropicClamp};
    }

    void BlendingApplication::SetupMaterials()
    {
        std::unique_ptr<Material> grass = std::make_unique<Material>();
        grass->Name = "grass";
        grass->MaterialCbIndex = 0;
        grass->DiffuseSrvHeapIndex = 1;
        grass->DiffuseAlbedo = DirectX::XMFLOAT4{1.f, 1.f, 1.f, 1.f};
        grass->FresnelR0 = DirectX::XMFLOAT3{0.01f, 0.01f, 0.01f};
        grass->Roughness = 0.125f;
        
        // Exercise 8.16.2
        grass->Roughness = 0.875f;
        
        // Not a good water material, still some tools and techniques to learn
        std::unique_ptr<Material> water = std::make_unique<Material>();
        water->Name = "water";
        water->MaterialCbIndex = 1;
        water->DiffuseSrvHeapIndex = 2;
        water->DiffuseAlbedo = DirectX::XMFLOAT4{1.f, 1.f, 1.f, 0.7f};
        water->FresnelR0 = DirectX::XMFLOAT3{0.1f, 0.1f, 0.1f};
        water->Roughness = 0.f;
        
        std::unique_ptr<Material> crate = std::make_unique<Material>();
        crate->Name = "crate";
        crate->MaterialCbIndex = 2;
        crate->DiffuseSrvHeapIndex = 0;
        crate->DiffuseAlbedo = DirectX::XMFLOAT4{1.f, 1.f, 1.f, 1.f};
        crate->FresnelR0 = DirectX::XMFLOAT3{0.01f, 0.01f, 0.01f};
        crate->Roughness = 0.125f;
        
        std::unique_ptr<Material> cage = std::make_unique<Material>();
        cage->Name = "cage";
        cage->MaterialCbIndex = 3;
        cage->DiffuseSrvHeapIndex = 3;
        cage->DiffuseAlbedo = DirectX::XMFLOAT4{1.f, 1.f, 1.f, 1.f};
        cage->FresnelR0 = DirectX::XMFLOAT3{0.01f, 0.01f, 0.01f};
        cage->Roughness = 0.125f;
        
        m_Materials["grass"] = std::move(grass);
        m_Materials["water"] = std::move(water);
        m_Materials["crate"] = std::move(crate);
        m_Materials["cage"] = std::move(cage);
    }

    void BlendingApplication::SetupLandGeometry()
    {
        GeometryGenerator generator{};
        
        GeometryGenerator::MeshData grid = generator.CreateGrid(160.f, 160.f, 50, 50);
        
        // Extract the vertex elements we are interested and apply the height function to each vertex
        // also color vertices based on their heights so we have sand, grass and snow mountain peaks
        
        std::vector<Vertex> vertices{grid.Vertices.size()};
        for (size_t i = 0; i < grid.Vertices.size(); i++)
        {
            DirectX::XMFLOAT3 position = grid.Vertices[i].Position;

            vertices[i].Position = position;
            vertices[i].Position.y = GetHillsHeight(position.x, position.z);
            vertices[i].Color = GetHillsColor(vertices[i].Position.y);
            vertices[i].Normal = GetHillsNormal(position.x, position.z);
            vertices[i].TexCoord = grid.Vertices[i].TexC;
        }
        
        const UINT vertexBufferByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
        
        std::vector<uint16_t> indices = grid.GetIndices16();
        const UINT indexBufferByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

        std::unique_ptr<MeshGeometry> geometry = std::make_unique<MeshGeometry>();
        geometry->Name = "landGeo";
        
        ThrowIfFailed(D3DCreateBlob(vertexBufferByteSize, &geometry->VertexBufferCPU));
        CopyMemory(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertexBufferByteSize);
        
        ThrowIfFailed(D3DCreateBlob(indexBufferByteSize, &geometry->IndexBufferCPU));
        CopyMemory(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), indexBufferByteSize);
        
        geometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(), m_CommandList.Get(), vertices.data(), vertexBufferByteSize, geometry->VertexBufferUploader);
        geometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(), m_CommandList.Get(), indices.data(), indexBufferByteSize, geometry->IndexBufferUploader);
        
        geometry->VertexByteStride = sizeof(Vertex);
        geometry->VertexBufferByteSize = vertexBufferByteSize;
        geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
        geometry->IndexBufferByteSize = indexBufferByteSize;
        
        SubmeshGeometry submesh{};
        submesh.IndexCount = static_cast<UINT>(indices.size());
        submesh.StartIndexLocation = 0;
        submesh.BaseVertexLocation = 0;
        
        geometry->DrawArgs["grid"] = submesh;
        
        m_Geometries["landGeo"] = std::move(geometry);
    }

    void BlendingApplication::SetupWaves()
    {
        m_Waves = std::make_unique<Waves>(128, 128, 1.f, 0.03f, 4.f, 0.2f);
        
        std::vector<std::uint16_t> indices(3 * m_Waves->TriangleCount()); // 3 indices per face
        assert(m_Waves->TriangleCount() < 0x0000ffff);
        
        // Iterate over each quad
        int m = m_Waves->RowCount();
        int n = m_Waves->ColumnCount();
        int k = 0;
        
        for (int i = 0; i < m - 1; i++)
        {
            for (int j = 0; j < n - 1; j++)
            {
                indices[k] = i * n + j;
                indices[k + 1] = i * n + j + 1;
                indices[k + 2] = (i + 1) * n + j;
                
                indices[k + 3] = (i + 1) * n + j;
                indices[k + 4] = i * n + j + 1;
                indices[k + 5] = (i + 1) * n + j + 1;
                
                k += 6; // Next quad
            }
        }
        
        UINT vertexBufferByteSize = m_Waves->VertexCount() * sizeof(Vertex);
        UINT indexBufferByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

        std::unique_ptr<MeshGeometry> geometry = std::make_unique<MeshGeometry>();
        geometry->Name = "waterGeo";
        
        // Set dynamically
        geometry->VertexBufferCPU = nullptr;
        geometry->VertexBufferGPU = nullptr;
        
        ThrowIfFailed(D3DCreateBlob(indexBufferByteSize, &geometry->IndexBufferCPU));
        CopyMemory(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), indexBufferByteSize);
        
        geometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(), m_CommandList.Get(), indices.data(), indexBufferByteSize, geometry->IndexBufferUploader);
        
        geometry->VertexByteStride = sizeof(Vertex);
        geometry->VertexBufferByteSize = vertexBufferByteSize;
        geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
        geometry->IndexBufferByteSize = indexBufferByteSize;
        
        SubmeshGeometry submesh{};
        submesh.IndexCount = static_cast<UINT>(indices.size());
        submesh.StartIndexLocation = 0;
        submesh.BaseVertexLocation = 0;
        
        geometry->DrawArgs["grid"] = submesh;
        
        m_Geometries["waterGeo"] = std::move(geometry);
    }

    void BlendingApplication::SetupCrate()
    {
        GeometryGenerator generator{};
        GeometryGenerator::MeshData crate = generator.CreateBox(6.f, 6.f, 6.f, 0);
        
        std::vector<Vertex> vertices{crate.Vertices.size()};

        for (int i = 0; i < crate.Vertices.size(); i++)
        {
            vertices[i].Position = crate.Vertices[i].Position;
            vertices[i].Color = DirectX::XMFLOAT4{1.f, 1.f, 1.f, 1.f};
            vertices[i].Normal = crate.Vertices[i].Normal;
            vertices[i].TexCoord = crate.Vertices[i].TexC;
        }
        
        UINT vertexBufferByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);

        std::vector<GeometryGenerator::uint16> indices = crate.GetIndices16();
        UINT indexBufferByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);
        
        std::unique_ptr<MeshGeometry> geometry = std::make_unique<MeshGeometry>();
        geometry->Name = "crateGeo";
        
        ThrowIfFailed(D3DCreateBlob(vertexBufferByteSize, &geometry->VertexBufferCPU));
        CopyMemory(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertexBufferByteSize);
        
        ThrowIfFailed(D3DCreateBlob(indexBufferByteSize, &geometry->IndexBufferCPU));
        CopyMemory(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), indexBufferByteSize);
        
        geometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(), m_CommandList.Get(), vertices.data(), vertexBufferByteSize, geometry->VertexBufferUploader);
        geometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(), m_CommandList.Get(), indices.data(), indexBufferByteSize, geometry->IndexBufferUploader);
        
        geometry->VertexByteStride = sizeof(Vertex);
        geometry->VertexBufferByteSize = vertexBufferByteSize;
        geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
        geometry->IndexBufferByteSize = indexBufferByteSize;
        
        SubmeshGeometry submesh{};
        submesh.IndexCount = static_cast<UINT>(indices.size());
        submesh.StartIndexLocation = 0;
        submesh.BaseVertexLocation = 0;
        
        geometry->DrawArgs["crate"] = submesh;
        
        m_Geometries["crateGeo"] = std::move(geometry);
    }

    float BlendingApplication::GetHillsHeight(float x, float z)
    {
        return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
    }

    DirectX::XMFLOAT3 BlendingApplication::GetHillsNormal(float x, float z)
    {
        // n = (-df/dx, 1, -df/dz)

        float normalX = -0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z);
        float normalZ = -0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z);

        DirectX::XMFLOAT3 normal{normalX, 1.f, normalZ};
        DirectX::XMVECTOR unitNormal = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&normal));
        DirectX::XMStoreFloat3(&normal, unitNormal);
        
        return normal;
    }

    DirectX::XMFLOAT4 BlendingApplication::GetHillsColor(float y)
    {
        if (y < -10.f)
        {
            // Sandy beach color
            return DirectX::XMFLOAT4{1.f, 0.96f, 0.62f, 1.f};    
        }

        if (y < 5.f)
        {
            // Light yellow-green
            return DirectX::XMFLOAT4{0.48f, 0.77f, 0.46f, 1.f};
        }
        
        if (y < 12.f)
        {
            // Dark yellow-green
            return DirectX::XMFLOAT4{0.1f, 0.48f, 0.19f, 1.f};
        }
        
        if (y < 20.f)
        {
            // Dark brown
            return DirectX::XMFLOAT4{0.45f, 0.39f, 0.34f, 1.f};
        }
        
        // White snow
        return DirectX::XMFLOAT4{1.f, 1.f, 1.f, 1.f};
    }

    void BlendingApplication::UpdateWaves()
    {
        // Every quarter second, generate a random wave
        static float timeBase = 0.f;

        if (m_Timer.GetTime() - timeBase > 0.25f)
        {
            timeBase += 0.25f;
            int i = MathHelper::Rand(4, m_Waves->RowCount() - 5);
            int j = MathHelper::Rand(4, m_Waves->ColumnCount() - 5);
            
            float r = MathHelper::RandF(0.2f, 0.5f);
            
            m_Waves->Disturb(i, j, r);
        }
        
        m_Waves->Update(m_Timer.GetDeltaTime());
        
        // Update the wave vertex buffer with the new solution
        UploadBuffer<Vertex>* currentWavesVertexBuffer = m_WaveVerticesFrameResources[m_CurrentFrameResourceIndex].get();
        
        for (int i = 0; i < m_Waves->VertexCount(); i++)
        {
            Vertex vertex{};
            vertex.Position = m_Waves->Position(i);
            vertex.Color = DirectX::XMFLOAT4(DirectX::Colors::Blue);
            vertex.Normal = m_Waves->Normal(i);
            
            // Derives tex-coord from position by
            // mapping [-w/2, w/2] -> [0,1]
            vertex.TexCoord.x = 0.5f + vertex.Position.x / m_Waves->Width();
            vertex.TexCoord.y = 0.5f + vertex.Position.z / m_Waves->Depth();
            
            currentWavesVertexBuffer->CopyData(i, vertex);
        }
        
        // Set the dynamic vertex buffer of the wave render item to the current frame vertex buffer
        m_WavesRenderItem->Geometry->VertexBufferGPU = currentWavesVertexBuffer->GetResource();
    }

    void BlendingApplication::AnimateMaterials()
    {
        // Scroll water texture coordinates
        Material* waterMaterial = m_Materials["water"].get();
        
        float& tu = waterMaterial->MatTransform(3, 0);
        float& tv = waterMaterial->MatTransform(3, 1);
        
        tu += 0.1f * m_Timer.GetDeltaTime();
        tv += 0.02f * m_Timer.GetDeltaTime();
        
        if (tu >= 1.f)
        {
            tu -= 1.f;
        }
        
        if (tv >= 1.f)
        {
            tv -= 1.f;
        }
        
        waterMaterial->MatTransform(3, 0) = tu;
        waterMaterial->MatTransform(3, 1) = tv;
        
        waterMaterial->NumFramesDirty = Constants::NUM_FRAME_RESOURCES;
    }

    void BlendingApplication::SetupRenderItems()
    {
        std::unique_ptr<RenderItem> wavesRenderItem = std::make_unique<RenderItem>();
        wavesRenderItem->WorldMatrix = MathHelper::Identity4x4();
        wavesRenderItem->ObjectConstantBufferIndex = 0;
        wavesRenderItem->Material = m_Materials["water"].get();
        wavesRenderItem->Geometry = m_Geometries["waterGeo"].get();
        wavesRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wavesRenderItem->IndexCount = wavesRenderItem->Geometry->DrawArgs["grid"].IndexCount;
        wavesRenderItem->StartIndexLocation = wavesRenderItem->Geometry->DrawArgs["grid"].StartIndexLocation;
        wavesRenderItem->BaseVertexLocation = wavesRenderItem->Geometry->DrawArgs["grid"].BaseVertexLocation;
        
        m_WavesRenderItem = wavesRenderItem.get();
        m_AllRenderItems.emplace_back(std::move(wavesRenderItem));
        m_TransparentRenderItems.push_back(m_WavesRenderItem);
        
        std::unique_ptr<RenderItem> landRenderItem = std::make_unique<RenderItem>();
        landRenderItem->WorldMatrix = MathHelper::Identity4x4();
        DirectX::XMStoreFloat4x4(&landRenderItem->TexTransform, DirectX::XMMatrixScaling(5.f, 5.f, 1.f));
        landRenderItem->ObjectConstantBufferIndex = 1;
        landRenderItem->Material = m_Materials["grass"].get();
        landRenderItem->Geometry = m_Geometries["landGeo"].get();
        landRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        landRenderItem->IndexCount = landRenderItem->Geometry->DrawArgs["grid"].IndexCount;
        landRenderItem->StartIndexLocation = landRenderItem->Geometry->DrawArgs["grid"].StartIndexLocation;
        landRenderItem->BaseVertexLocation = landRenderItem->Geometry->DrawArgs["grid"].BaseVertexLocation;
        m_OpaqueRenderItems.push_back(landRenderItem.get());
        m_AllRenderItems.emplace_back(std::move(landRenderItem));
        
        std::unique_ptr<RenderItem> crateRenderItem = std::make_unique<RenderItem>();
        DirectX::XMStoreFloat4x4(&crateRenderItem->WorldMatrix, DirectX::XMMatrixTranslation(-4.0f, 1.0f, 0.0f));
        crateRenderItem->ObjectConstantBufferIndex = 2;
        crateRenderItem->Material = m_Materials["crate"].get();
        crateRenderItem->Geometry = m_Geometries["crateGeo"].get();
        crateRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        crateRenderItem->IndexCount = crateRenderItem->Geometry->DrawArgs["crate"].IndexCount;
        crateRenderItem->StartIndexLocation = crateRenderItem->Geometry->DrawArgs["crate"].StartIndexLocation;
        crateRenderItem->BaseVertexLocation = crateRenderItem->Geometry->DrawArgs["crate"].BaseVertexLocation;
        m_OpaqueRenderItems.push_back(crateRenderItem.get());
        m_AllRenderItems.emplace_back(std::move(crateRenderItem));
        
        std::unique_ptr<RenderItem> cageRenderItem = std::make_unique<RenderItem>();
        DirectX::XMStoreFloat4x4(&cageRenderItem->WorldMatrix, DirectX::XMMatrixTranslation(-12.0f, 1.0f, 0.0f));
        cageRenderItem->ObjectConstantBufferIndex = 3;
        cageRenderItem->Material = m_Materials["cage"].get();
        cageRenderItem->Geometry = m_Geometries["crateGeo"].get();
        cageRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        cageRenderItem->IndexCount = cageRenderItem->Geometry->DrawArgs["crate"].IndexCount;
        cageRenderItem->StartIndexLocation = cageRenderItem->Geometry->DrawArgs["crate"].StartIndexLocation;
        cageRenderItem->BaseVertexLocation = cageRenderItem->Geometry->DrawArgs["crate"].BaseVertexLocation;
        m_AlphaTestedRenderItems.push_back(cageRenderItem.get());
        m_AllRenderItems.emplace_back(std::move(cageRenderItem));
    }
    
    void BlendingApplication::SetupShaderAndInputLayout()
    {
        const std::wstring shaderPath = L"data//shaders//blendApp.hlsl";
        
        // Exercise 8.16.6
        D3D_SHADER_MACRO defaultDefines[] = {
            "FOG", "1",
            "TOON_SHADING", "0", 
            nullptr, nullptr
        };

        m_VertexShaderBytecodes["default"] = ShaderUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
        m_PixelShaderBytecodes["default"] = ShaderUtil::CompileShader(shaderPath, defaultDefines, "PS", "ps_5_0");
        
        D3D_SHADER_MACRO alphaTestedDefines[] = {
            "FOG", "1",
            "ALPHA_TEST", "1", 
            nullptr, nullptr
        };

        // There is a cost using alpha test, so we specialize the shader and only use it if we need it
        m_PixelShaderBytecodes["alphaTested"] = ShaderUtil::CompileShader(shaderPath, alphaTestedDefines, "PS", "ps_5_0");
        
        m_InputElementDescriptions = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
    }

    void BlendingApplication::CreatePipelineStateObjects()
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePipelineStateDesc{};
        ZeroMemory(&opaquePipelineStateDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        
        opaquePipelineStateDesc.InputLayout = {
            m_InputElementDescriptions.data(),
            static_cast<UINT>(m_InputElementDescriptions.size())
        };
        
        opaquePipelineStateDesc.pRootSignature = m_RootSignature.Get();
        
        opaquePipelineStateDesc.VS = {
            m_VertexShaderBytecodes["default"]->GetBufferPointer(),
            m_VertexShaderBytecodes["default"]->GetBufferSize()
        };
        
        opaquePipelineStateDesc.PS = {
            m_PixelShaderBytecodes["default"]->GetBufferPointer(),
            m_PixelShaderBytecodes["default"]->GetBufferSize()
        };
        
        opaquePipelineStateDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        opaquePipelineStateDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        opaquePipelineStateDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        opaquePipelineStateDesc.SampleMask = UINT_MAX;
        opaquePipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        opaquePipelineStateDesc.NumRenderTargets = 1;
        opaquePipelineStateDesc.RTVFormats[0] = m_BackBufferFormat;
        opaquePipelineStateDesc.SampleDesc.Count = 1;
        opaquePipelineStateDesc.SampleDesc.Quality = 0;
        opaquePipelineStateDesc.DSVFormat = m_DepthStencilFormat;
        
        ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&opaquePipelineStateDesc, IID_PPV_ARGS(&m_PipelineStateObjects["opaque"])));
        
        D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframeStateDesc = opaquePipelineStateDesc;
        opaqueWireframeStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        
        ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&opaqueWireframeStateDesc, IID_PPV_ARGS(&m_PipelineStateObjects["opaque_wireframe"])));
        
        D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPipelineStateDesc = opaquePipelineStateDesc;
        D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc{};
        transparencyBlendDesc.BlendEnable = true;
        // Logic operators instead of traditional blending equations, like AND, NOR, XOR...
        // BlendEnable and LogicOpEnabled can't be enabled at the same time, we must choose one or another (same for BlendOp and LogicOp - NOOP when not used)
        transparencyBlendDesc.LogicOpEnable = false;
        
        // Blending Equation
        // C = Csrc (X) Fsrc [BlendOp] Cdst (X) Fdst
        // Csrc = Color source, the color pixel shader has just computed
        // (X) = Component wise multiplication
        // Fsrc = Source factor to be used on the component wise multiplication (SrcBlend)
        // [BlendOp] = BlendOp used, OP_ADD for example would turn into C = Csr (X) Fsrc + Cdst (X) Fdst
        // Cdst = Color destination, the color pixel on current render target
        // Fdst = Destination factor to be used on the component wise multiplication (DestBlend)
        transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;

        // We also have a separate similar blending equation for the alpha component 
        // A = Asrc * Fsrc [BlendOpAlpha] Adst * Fdst
        transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
        transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
        transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;

        // NOOP since we have BlendEnabled
        transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
        
        // We can control which color channels in the back buffer are written after blending
        // For example, we could disable writes to RGB channel and only write to alpha channel (D3D12_COLOR_WRITE_ENABLE_ALPHA) for some techniques
        transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        //transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_BLUE | D3D12_COLOR_WRITE_ENABLE_ALPHA; // Exercise 10.10.5
        
        // Multisample technique useful when rendering foliage or gate textures (requires multisample to be enabled - back buffer and depth buffer)
        transparentPipelineStateDesc.BlendState.AlphaToCoverageEnable = false;
        
        // Direct3D supports rendering to 8 render targets simultaneously. If we set this flag to true, blending can be performed for each
        // render target differently (different blend op, factors...). False means all render targets are going to use same D3D12_RENDER_TARGET_BLEND_DESC as the first
        // element from BlendState.RenderTarget array
        transparentPipelineStateDesc.BlendState.IndependentBlendEnable = false;

        transparentPipelineStateDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
        
        ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&transparentPipelineStateDesc, IID_PPV_ARGS(&m_PipelineStateObjects["transparent"])));
        
        D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPipelineStateDesc = opaquePipelineStateDesc;

        alphaTestedPipelineStateDesc.PS = {
        m_PixelShaderBytecodes["alphaTested"]->GetBufferPointer(),
        m_PixelShaderBytecodes["alphaTested"]->GetBufferSize()
        };

        alphaTestedPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        
        ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&alphaTestedPipelineStateDesc, IID_PPV_ARGS(&m_PipelineStateObjects["alphaTested"])));
    }
}
