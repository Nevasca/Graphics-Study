#include "LandAndWavesApplication.h"

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
    void LandAndWavesApplication::Initialize(HWND mainWindow, int windowWidth, int windowHeight)
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
        
        SetupLandGeometry();
        SetupRenderItems();
        CreateFrameResources();
        
        CreateDescriptorHeaps();
        CreateConstantBufferViews();

        CreateRootSignature();
        SetupShaderAndInputLayout();
        CreatePipelineStateObjects();

        m_Timer.Reset();

        // Execute the initialization commands
        ThrowIfFailed(m_CommandList->Close());
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);

        FlushCommandQueue();
    }

    void LandAndWavesApplication::Tick()
    {
        m_Timer.Tick();
        
        CalculateFrameStats();
        
        SetNextFrameResource();
        
        UpdateCamera();
        UpdateObjectConstantBuffers();
        UpdatePassConstantBuffer();
        
        m_IsWireframe = Input::GetKeyboardKey('1'); 

        Draw();
    }

    void LandAndWavesApplication::SetNextFrameResource()
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

    void LandAndWavesApplication::Draw()
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
            DirectX::Colors::LightSteelBlue,
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

        ID3D12DescriptorHeap* descriptorHeaps[] = { m_CbvDescriptorHeap.Get() };
        m_CommandList->SetDescriptorHeaps(1, descriptorHeaps);
        
        m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());
        
        int passCbvIndex = static_cast<int>(m_PassCbvOffset) + m_CurrentFrameResourceIndex;
        CD3DX12_GPU_DESCRIPTOR_HANDLE passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE{m_CbvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()};
        passCbvHandle.Offset(passCbvIndex, m_CbvSrvDescriptorSize);
        m_CommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);
        
        DrawRenderItems(*m_CommandList.Get(), m_OpaqueRenderItems);
        
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

    void LandAndWavesApplication::DrawRenderItems(ID3D12GraphicsCommandList& commandList, const std::vector<RenderItem*>& renderItems)
    {
        UINT objectConstantBufferByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

        ID3D12Resource* objectConstantBuffer = m_CurrentFrameResource->ObjectConstantBuffer->GetResource();
        
        for(size_t i = 0; i < renderItems.size(); i++)
        {
            RenderItem* renderItem = renderItems[i];

            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = renderItem->Geometry->GetVertexBufferView();
            commandList.IASetVertexBuffers(0, 1, &vertexBufferView);
            
            D3D12_INDEX_BUFFER_VIEW indexBufferView = renderItem->Geometry->GetIndexBufferView();
            commandList.IASetIndexBuffer(&indexBufferView);
            
            commandList.IASetPrimitiveTopology(renderItem->PrimitiveTopology);
            
            // Offset to the CBV in the descriptor for this object and frame resource
            UINT cbvIndex = m_CurrentFrameResourceIndex * static_cast<UINT>(m_OpaqueRenderItems.size()) + renderItem->ObjectConstantBufferIndex;
            CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE{
                m_CbvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
            };
            cbvHandle.Offset(cbvIndex, m_CbvSrvDescriptorSize);
            commandList.SetGraphicsRootDescriptorTable(0, cbvHandle);
            
            commandList.DrawIndexedInstanced(
                renderItem->IndexCount,
                1,
                renderItem->StartIndexLocation,
                renderItem->BaseVertexLocation,
                0);
        }
    }

    void LandAndWavesApplication::UpdateObjectConstantBuffers()
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

    void LandAndWavesApplication::UpdatePassConstantBuffer()
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

    void LandAndWavesApplication::UpdateCamera()
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
            // Make each pixel correspond to 0.005 unit in the scene
            float dx = 0.005f * (currentMousePosition.X - static_cast<float>(m_LastMousePos.x));
            float dy = 0.005f * (currentMousePosition.Y - static_cast<float>(m_LastMousePos.y));

            m_Radius += dx - dy;

            m_Radius = MathHelper::Clamp(m_Radius, 3.0f, 15.f);
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

    void LandAndWavesApplication::CreateRootSignature()
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

    void LandAndWavesApplication::CreateFrameResources()
    {
        UINT objectCount = static_cast<UINT>(m_AllRenderItems.size());

        for(int i = 0; i < Constants::NUM_FRAME_RESOURCES; i++)
        {
            m_FrameResources.emplace_back(std::make_unique<FrameResource>(*m_Device.Get(), 1, objectCount));
        }
    }
    
    void LandAndWavesApplication::CreateDescriptorHeaps()
    {
        UINT objectCount = static_cast<UINT>(m_OpaqueRenderItems.size());
        
        // Need a CBV descriptor for each object for each frame resource
        // +1 for the per pass CBV for each frame resource
        UINT numDescriptors = Constants::NUM_FRAME_RESOURCES * (objectCount + 1);
        
        // Save offset to start of pass cbvs, the last three descriptors
        m_PassCbvOffset = objectCount * Constants::NUM_FRAME_RESOURCES;
        
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = numDescriptors;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbvHeapDesc.NodeMask = 0;
        
        ThrowIfFailed(m_Device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_CbvDescriptorHeap)));
    }

    void LandAndWavesApplication::CreateConstantBufferViews()
    {
        UINT objectConstantBufferSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
        UINT objectCount = static_cast<UINT>(m_OpaqueRenderItems.size());
        
        // Create constant buffer views for each object for each frame resource
        // 0 to n-1 contains CBVs for objects of the 0th frame, 2n-1 for the 1st, 3n-1 for the 2nd
        // 3n, 3n+1 and 3n+2 contains pass for 0th, 1st and 2nd frames
        for (int frameIndex = 0; frameIndex < Constants::NUM_FRAME_RESOURCES; frameIndex++)
        {
            ID3D12Resource* objectConstantBuffer = m_FrameResources[frameIndex]->ObjectConstantBuffer->GetResource();
            
            for (UINT objectIndex = 0; objectIndex < objectCount; objectIndex++)
            {
                D3D12_GPU_VIRTUAL_ADDRESS constantBufferGPUAddress = objectConstantBuffer->GetGPUVirtualAddress();
                constantBufferGPUAddress += objectIndex * objectConstantBufferSize;
                
                int heapIndex = frameIndex * objectCount + objectIndex;
                CD3DX12_CPU_DESCRIPTOR_HANDLE handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{
                    m_CbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()
                };
                handle.Offset(heapIndex, m_CbvSrvDescriptorSize);
                
                D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc = {};
                constantBufferViewDesc.BufferLocation = constantBufferGPUAddress;
                constantBufferViewDesc.SizeInBytes = objectConstantBufferSize;
                
                m_Device->CreateConstantBufferView(&constantBufferViewDesc, handle);
            }
        }
        
        // Last three descriptors are the pass CBVs for each frame resource
        UINT passConstantBufferSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
        for (int frameIndex = 0; frameIndex < Constants::NUM_FRAME_RESOURCES; frameIndex++)
        {
            ID3D12Resource* passConstantBuffer = m_FrameResources[frameIndex]->PassConstantBuffer->GetResource();
            
            D3D12_GPU_VIRTUAL_ADDRESS constantBufferGPUAddress = passConstantBuffer->GetGPUVirtualAddress();

            int heapIndex = static_cast<int>(m_PassCbvOffset) + frameIndex;
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{m_CbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()};
            handle.Offset(heapIndex, m_CbvSrvDescriptorSize);
            
            D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc = {};
            constantBufferViewDesc.BufferLocation = constantBufferGPUAddress;
            constantBufferViewDesc.SizeInBytes = passConstantBufferSize;
            
            m_Device->CreateConstantBufferView(&constantBufferViewDesc, handle);
        }
    }

    void LandAndWavesApplication::SetupLandGeometry()
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
    
    float LandAndWavesApplication::GetHillsHeight(float x, float z)
    {
        return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
    }

    DirectX::XMFLOAT4 LandAndWavesApplication::GetHillsColor(float y)
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

    void LandAndWavesApplication::SetupRenderItems()
    {
        using namespace DirectX;

        std::unique_ptr<RenderItem> landRenderItem = std::make_unique<RenderItem>();
        landRenderItem->WorldMatrix = MathHelper::Identity4x4();
        landRenderItem->ObjectConstantBufferIndex = 0;
        landRenderItem->Geometry = m_Geometries["landGeo"].get();
        landRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        landRenderItem->IndexCount = landRenderItem->Geometry->DrawArgs["grid"].IndexCount;
        landRenderItem->StartIndexLocation = landRenderItem->Geometry->DrawArgs["grid"].StartIndexLocation;
        landRenderItem->BaseVertexLocation = landRenderItem->Geometry->DrawArgs["grid"].BaseVertexLocation;
        m_AllRenderItems.emplace_back(std::move(landRenderItem));
        
        // All render items are opaque in this demo
        for (const auto& renderItem : m_AllRenderItems)
        {
            m_OpaqueRenderItems.push_back(renderItem.get());
        }
    }
    
    void LandAndWavesApplication::SetupShaderAndInputLayout()
    {
        const std::wstring shaderPath = L"data//colorShapesApp.hlsl";

        m_VertexShaderBytecode = ShaderUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
        m_PixelShaderBytecode = ShaderUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");
        
        m_InputElementDescriptions = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };
    }

    void LandAndWavesApplication::CreatePipelineStateObjects()
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePipelineStateDesc{};
        ZeroMemory(&opaquePipelineStateDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        
        opaquePipelineStateDesc.InputLayout = {
            m_InputElementDescriptions.data(),
            static_cast<UINT>(m_InputElementDescriptions.size())
        };
        
        opaquePipelineStateDesc.pRootSignature = m_RootSignature.Get();
        
        opaquePipelineStateDesc.VS = {
            m_VertexShaderBytecode->GetBufferPointer(),
            m_VertexShaderBytecode->GetBufferSize()
        };
        
        opaquePipelineStateDesc.PS = {
            m_PixelShaderBytecode->GetBufferPointer(),
            m_PixelShaderBytecode->GetBufferSize()
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
    }
}
