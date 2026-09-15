#pragma once
#include "video/VideoTypes.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
namespace reagba {
class D3DRenderer {
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> nearest_, linear_;
    int width_ = 0, height_ = 0;

  public:
    explicit D3DRenderer(HWND);
    void Resize(int, int);
    void Upload(const Frame &);
    void Draw(VideoSettings);
};
} // namespace reagba
