#define STB_IMAGE_IMPLEMENTATION
#include "../deps/stb_image.h"

#include <d3d11.h>
#include "image.h"

// XISFPS port: FreeFire keeps its D3D11 device inside namespace GlobalState
// in main.cpp. Extern-declare it in that namespace and pull it into scope so
// the ported image loader can upload textures.
namespace GlobalState { extern ID3D11Device* g_pd3dDevice; }
using GlobalState::g_pd3dDevice;

ImTextureID c_image::load(const unsigned char* bytes, int len)
{
    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load_from_memory(bytes, len, &w, &h, &c, 4);
    if (!data)
        return nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = (UINT)w;
    desc.Height           = (UINT)h;
    desc.MipLevels        = 0;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags        = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ID3D11Texture2D*          tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;

    if (g_pd3dDevice && SUCCEEDED(g_pd3dDevice->CreateTexture2D(&desc, nullptr, &tex)))
    {
        ID3D11DeviceContext* ctx = nullptr;
        g_pd3dDevice->GetImmediateContext(&ctx);
        ctx->UpdateSubresource(tex, 0, nullptr, data, (UINT)(w * 4), 0);

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels       = (UINT)-1;
        srv_desc.Texture2D.MostDetailedMip = 0;
        g_pd3dDevice->CreateShaderResourceView(tex, &srv_desc, &srv);

        ctx->GenerateMips(srv);
        ctx->Release();
        tex->Release();
    }

    stbi_image_free(data);
    return (ImTextureID)srv;
}
