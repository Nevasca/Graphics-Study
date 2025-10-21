#pragma once
#include <d3dcommon.h>
#include <string>
#include <wrl/client.h>

namespace Studies
{
    class ShaderUtil
    {
        public:
        static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
            const std::wstring& fileName,
            const D3D_SHADER_MACRO* defines,
            const std::string& entrypoint,
            const std::string& target);
    };
}
