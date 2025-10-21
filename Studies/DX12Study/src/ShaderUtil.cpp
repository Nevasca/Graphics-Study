#include "ShaderUtil.h"

#include <D3Dcompiler.h>
#include <d3dUtil.h>

namespace Studies
{
    // Compiles a shader at runtime
    // In Direct3D, shader programs must first be compiled to a portable bytecode
    // This function compiles a shader at runtime. For better loading times, as complex shaders can take a long time,
    // we should consider doing offline compilation (through a build step, an asset pipeline...)

    // If it throws error X3000: Illegal character in shader file pointing right to the first character
    // Check the file encoding. It should be UTF-8 with no BOM (UTF-8, not UTF-8-BOM)
    Microsoft::WRL::ComPtr<ID3DBlob> ShaderUtil::CompileShader(
        const std::wstring& fileName,
        const D3D_SHADER_MACRO* defines,
        const std::string& entrypoint,
        const std::string& target)
    {
        // Use debug flags in debug mode
        UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;        
#endif

        Microsoft::WRL::ComPtr<ID3DBlob> byteCode = nullptr;
        Microsoft::WRL::ComPtr<ID3DBlob> errorMessages = nullptr;

        /*
         * pFileName: the name of the HLSL source code file that we want to compile
         * pDefines: advanced options, check SDK documentation
         * pInclude: advanced options, check SDK documentation. We are going to use the default one
         * pEntrypoint: the function name of the shader's entry point. A HLSL file can contain multiple shader programs (vertex, pixel)
         * so we need to specify the entry point of the one we are interested
         * pTarget: string specifying the shader program type and version we are using, such as:
         *          vs_5_0 and vs_5_1 (Vertex shader 5.0 and 5.1)
         *          hs_5_0 and hs_5_1 (Hull shader 5.0 and 5.1)
         *          ds_5_0 and ds_5_1 (Domain shader 5.0 and 5.1)
         *          gs_5_0 and gs_5_1 (Geometry shader 5.0 and 5.1)
         *          ps_5_0 and ps_5_1 (Pixel shader 5.0 and 5.1)
         *          cs_5_0 and cs_5_1 (Compute shader 5.0 and 5.1)
         * Flags1: flags to specify how the shader code should be compiled. We can set flags for compiling in debug mode, useful for debugging
         * Flags2: advanced options, check SDK documentation
         * ppCode: returns a pointer to a ID3DBlob data structure that stores the compiled shader bytecode
         * ppErrorMgs: returns a pointer to a iD3DBlob data structure that stores a string containing the compilation errors, if any
        */
        HRESULT hr = S_OK;
        hr = D3DCompileFromFile(
            fileName.c_str(),
            defines,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entrypoint.c_str(),
            target.c_str(),
            compileFlags,
            0,
            &byteCode,
            &errorMessages);

        if (errorMessages != nullptr)
        {
            OutputDebugStringA(static_cast<char*>(errorMessages->GetBufferPointer()));
        }

        ThrowIfFailed(hr);

        return byteCode;
    }
}
