#include "LitWavesApplication.h"

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
    void LitWavesApplication::Initialize(HWND mainWindow, int windowWidth, int windowHeight)
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
        
        SetupMaterials();
        SetupLandGeometry();
        SetupWaves();
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
    }

    void LitWavesApplication::Tick()
    {
        m_Timer.Tick();
        
        CalculateFrameStats();
        
        SetNextFrameResource();
        
        UpdateCamera();
        UpdateSun();

        UpdateObjectConstantBuffers();
        UpdatePassConstantBuffer();
        UpdateMaterialConstantBuffers();
        UpdateWaves();
        
        m_IsWireframe = Input::GetKeyboardKey('1'); 

        Draw();
    }

    void LitWavesApplication::SetNextFrameResource()
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

    void LitWavesApplication::Draw()
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

        m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

        ID3D12Resource* passConstantBuffer = m_CurrentFrameResource->PassConstantBuffer->GetResource();
        m_CommandList->SetGraphicsRootConstantBufferView(2, passConstantBuffer->GetGPUVirtualAddress());
        
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

    void LitWavesApplication::DrawRenderItems(ID3D12GraphicsCommandList& commandList, const std::vector<RenderItem*>& renderItems)
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
            
            D3D12_GPU_VIRTUAL_ADDRESS objectConstantBufferAddress = objectConstantBuffer->GetGPUVirtualAddress();
            objectConstantBufferAddress += renderItem->ObjectConstantBufferIndex * objectConstantBufferByteSize;
            commandList.SetGraphicsRootConstantBufferView(0, objectConstantBufferAddress);
            
            D3D12_GPU_VIRTUAL_ADDRESS materialConstantBufferAddress = materialConstantBuffer->GetGPUVirtualAddress();
            materialConstantBufferAddress += renderItem->Material->MaterialCbIndex * materialConstantBufferByteSize;
            commandList.SetGraphicsRootConstantBufferView(1, materialConstantBufferAddress);
            
            commandList.DrawIndexedInstanced(
                renderItem->IndexCount,
                1,
                renderItem->StartIndexLocation,
                renderItem->BaseVertexLocation,
                0);
        }
    }

    void LitWavesApplication::UpdateObjectConstantBuffers()
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

    void LitWavesApplication::UpdatePassConstantBuffer()
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
        constexpr float pulseSpeed = 10.f;
        float pulseFactor = 0.5f + sinf(pulseSpeed * m_Timer.GetTime()) / 2.f;
        m_MainPassConstants.Lights[0].Strength = DirectX::XMFLOAT3{1.0f * pulseFactor, 0.0f, 0.0f};
        
        UploadBuffer<PassConstants>* currentPassConstantBuffer = m_CurrentFrameResource->PassConstantBuffer.get();
        currentPassConstantBuffer->CopyData(0, m_MainPassConstants);
    }

    void LitWavesApplication::UpdateMaterialConstantBuffers()
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
            
            currentMaterialConstantBuffer->CopyData(material->MaterialCbIndex, materialConstants);
            
            material->NumFramesDirty--;
        }
    }

    void LitWavesApplication::UpdateCamera()
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

    void LitWavesApplication::UpdateSun()
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

    void LitWavesApplication::CreateRootSignature()
    {
        // We should not go overboard with number of constant buffers in our shaders for performance reasons
        // It's recommended to keep them under five

        CD3DX12_ROOT_PARAMETER rootParameters[3];
        rootParameters[0].InitAsConstantBufferView(0); // per-object CBV
        rootParameters[1].InitAsConstantBufferView(1); //per-material CBV
        rootParameters[2].InitAsConstantBufferView(2); //per-pass CBV
        
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc
        {
            3,
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

    void LitWavesApplication::CreateFrameResources()
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

    void LitWavesApplication::SetupMaterials()
    {
        std::unique_ptr<Material> grass = std::make_unique<Material>();
        grass->Name = "grass";
        grass->MaterialCbIndex = 0;
        grass->DiffuseAlbedo = DirectX::XMFLOAT4{0.2f, 0.6f, 0.6f, 1.f};
        grass->FresnelR0 = DirectX::XMFLOAT3{0.01f, 0.01f, 0.01f};
        grass->Roughness = 0.125f;
        
        // Exercise 8.16.2
        grass->Roughness = 0.875f;
        
        // Not a good water material, still some tools and techniques to learn
        std::unique_ptr<Material> water = std::make_unique<Material>();
        water->Name = "water";
        water->MaterialCbIndex = 1;
        water->DiffuseAlbedo = DirectX::XMFLOAT4{0.f, 0.2f, 0.6f, 1.f};
        water->FresnelR0 = DirectX::XMFLOAT3{0.1f, 0.1f, 0.1f};
        water->Roughness = 0.f;
        
        m_Materials["grass"] = std::move(grass);
        m_Materials["water"] = std::move(water);
    }

    void LitWavesApplication::SetupLandGeometry()
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

    void LitWavesApplication::SetupWaves()
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

    float LitWavesApplication::GetHillsHeight(float x, float z)
    {
        return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
    }

    DirectX::XMFLOAT3 LitWavesApplication::GetHillsNormal(float x, float z)
    {
        // n = (-df/dx, 1, -df/dz)

        float normalX = -0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z);
        float normalZ = -0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z);

        DirectX::XMFLOAT3 normal{normalX, 1.f, normalZ};
        DirectX::XMVECTOR unitNormal = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&normal));
        DirectX::XMStoreFloat3(&normal, unitNormal);
        
        return normal;
    }

    DirectX::XMFLOAT4 LitWavesApplication::GetHillsColor(float y)
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

    void LitWavesApplication::UpdateWaves()
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
            
            currentWavesVertexBuffer->CopyData(i, vertex);
        }
        
        // Set the dynamic vertex buffer of the wave render item to the current frame vertex buffer
        m_WavesRenderItem->Geometry->VertexBufferGPU = currentWavesVertexBuffer->GetResource();
    }

    void LitWavesApplication::SetupRenderItems()
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
        
        std::unique_ptr<RenderItem> landRenderItem = std::make_unique<RenderItem>();
        landRenderItem->WorldMatrix = MathHelper::Identity4x4();
        landRenderItem->ObjectConstantBufferIndex = 1;
        landRenderItem->Material = m_Materials["grass"].get();
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
    
    void LitWavesApplication::SetupShaderAndInputLayout()
    {
        const std::wstring shaderPath = L"data//litWavesApp.hlsl";

        m_VertexShaderBytecode = ShaderUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
        m_PixelShaderBytecode = ShaderUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");
        
        m_InputElementDescriptions = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };
    }

    void LitWavesApplication::CreatePipelineStateObjects()
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
