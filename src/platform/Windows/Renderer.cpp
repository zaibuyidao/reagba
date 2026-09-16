#include "platform/Windows/Renderer.h"
#include "video/BuiltinShaders.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace reagba {
using Microsoft::WRL::ComPtr;
static void Check(HRESULT r) {
    if (FAILED(r))
        throw std::runtime_error("Direct3D operation failed: " + std::to_string(unsigned(r)));
}
D3DRenderer::D3DRenderer(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    auto hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                            D3D11_SDK_VERSION, &desc, &swap_, &device_, nullptr, &context_);
    if (FAILED(hr))
        Check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                            D3D11_SDK_VERSION, &desc, &swap_, &device_, nullptr, &context_));
    ComPtr<IDXGIFactory> factory;
    if (SUCCEEDED(swap_->GetParent(IID_PPV_ARGS(&factory))))
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    const auto fragment = shaders::HLSLFragment();
    ComPtr<ID3DBlob> vs, ps, error;
    Check(D3DCompile(shaders::HLSLVertex, sizeof(shaders::HLSLVertex)-1, nullptr, nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vs, &error));
    Check(D3DCompile(fragment.data(), fragment.size(), nullptr, nullptr, nullptr, "ps", "ps_4_0", 0, 0, &ps, &error));
    Check(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_));
    Check(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel_));
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = sizeof(shaders::Uniforms);
    constants.Usage = D3D11_USAGE_DEFAULT;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Check(device_->CreateBuffer(&constants, nullptr, &uniforms_));
    D3D11_TEXTURE2D_DESC tex{};
    tex.Width = Width;
    tex.Height = Height;
    tex.MipLevels = 1;
    tex.ArraySize = 1;
    tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex.SampleDesc.Count = 1;
    tex.Usage = D3D11_USAGE_DEFAULT;
    tex.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Frame blank{};
    D3D11_SUBRESOURCE_DATA initial{blank.data(), Width * 4, 0};
    Check(device_->CreateTexture2D(&tex, &initial, &texture_));
    Check(device_->CreateShaderResourceView(texture_.Get(), nullptr, &resource_));
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    Check(device_->CreateSamplerState(&sampler, &nearest_));
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    Check(device_->CreateSamplerState(&sampler, &linear_));
}
void D3DRenderer::Resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == width_ && h == height_))
        return;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset();
    Check(swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0));
    ComPtr<ID3D11Texture2D> buffer;
    Check(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)));
    Check(device_->CreateRenderTargetView(buffer.Get(), nullptr, &target_));
    width_ = w;
    height_ = h;
}
void D3DRenderer::Upload(const Frame &frame) {
    context_->UpdateSubresource(texture_.Get(), 0, nullptr, frame.data(), Width * 4, 0);
}
void D3DRenderer::Draw(VideoSettings settings) {
    if (!target_)
        return;
    const float background[] = {0.027f, 0.039f, 0.055f, 1};
    context_->ClearRenderTargetView(target_.Get(), background);
    float scale = std::min(float(width_) / Width, float(height_) / Height);
    if (settings.integerScaling && scale >= 1)
        scale = std::floor(scale);
    D3D11_VIEWPORT view{
        (width_ - Width * scale) / 2, (height_ - Height * scale) / 2, Width * scale, Height * scale, 0, 1};
    context_->RSSetViewports(1, &view);
    auto *t = target_.Get();
    context_->OMSetRenderTargets(1, &t, nullptr);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_.Get(), nullptr, 0);
    auto *r = resource_.Get();
    context_->PSSetShaderResources(0, 1, &r);
    // Presets own their sampling; the plain texture filter is restored on "none".
    auto *s = settings.linear && settings.shader == ShaderPreset::Off ? linear_.Get() : nearest_.Get();
    context_->PSSetSamplers(0, 1, &s);
    const shaders::Uniforms values{float(Width), float(Height), view.Width, view.Height, int(settings.shader)};
    context_->UpdateSubresource(uniforms_.Get(), 0, nullptr, &values, 0, 0);
    auto *u = uniforms_.Get();
    context_->PSSetConstantBuffers(0, 1, &u);
    context_->Draw(3, 0);
    Check(swap_->Present(settings.vsync ? 1 : 0, 0));
}
} // namespace reagba
