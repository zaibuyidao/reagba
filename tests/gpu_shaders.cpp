// Standalone Windows GPU regression test, also built by CMake/CTest.
// Uses WARP offscreen: no REAPER, ROM, window or physical GPU is required.
#include "video/BuiltinShaders.h"
#include "video/VideoTypes.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace reagba;
static void Check(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("D3D failure: " + std::to_string(unsigned(hr)));
}
static ComPtr<ID3DBlob> Compile(const char* source, size_t size, const char* entry, const char* model) {
    ComPtr<ID3DBlob> code, log;
    auto hr = D3DCompile(source, size, nullptr, nullptr, nullptr, entry, model,
                         D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &log);
    if (FAILED(hr)) throw std::runtime_error(log ? static_cast<const char*>(log->GetBufferPointer()) : "Shader compile failed");
    return code;
}
// Independent double-precision reference: explicitly integrate the polynomial,
// rather than using the GPU's vectorized Horner implementation.
static double Coverage(double position, double footprint, double radius, bool horizontal) {
    const double x[] = {1.,-2./3,-1./5,4./7,-1./9,-2./11,1./13};
    const double y[] = {1.,0.,-4./5,2./7,4./9,-4./11,1./13};
    const auto* coefficients = horizontal ? x : y;
    auto integral = [&](double z) {
        double sum = 0;
        for (int i=0;i<7;++i) sum += coefficients[i]*std::pow(z,2*i+1);
        return sum;
    };
    return std::max(0., radius/footprint *
        (integral(std::clamp((position+footprint/2)/radius,-1.,1.)) -
         integral(std::clamp((position-footprint/2)/radius,-1.,1.))));
}
static double Reference(const Frame& input, int x, int y, int width, int height, int channel, int mode) {
    const double sx=(x+.5)*Width/width, sy=(y+.5)*Height/height;
    auto fetch = [&](int u, int v) {
        return ((input[std::clamp(v,0,Height-1)*Width+std::clamp(u,0,Width-1)]>>(channel*8))&255)/255.;
    };
    if (mode == 0) return fetch(int(sx),int(sy));
    if (mode == 1) {
        const double pi=3.141592654;
        return fetch(int(sx),int(sy)) * (4+std::sin(sx*2*pi+pi*(.5-channel*2./3)))/5 *
               (16+std::sin(sy*2*pi))/17;
    }
    const double px=sx-.4999, py=sy-.4999;
    const int ix=int(std::floor(px)), iy=int(std::floor(py));
    double light=0;
    for (int row=0;row<2;++row)
        for (int col=0;col<2;++col)
            light += std::pow(fetch(ix+col,iy+row)+.05,3) *
                Coverage((px-ix)*3+1-channel-col*3,3.*Width/width,1.5,true) *
                Coverage(py-iy-row,1.*Height/height,.63,false);
    return std::pow(std::max(0.,light),1/2.2);
}
int main(int argc, char** argv) try {
    const auto output=argc>1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
    if (!output.empty()) {
        std::filesystem::create_directories(output);
        std::ofstream(output/"vertex.glsl") << shaders::GLSLVertex;
        std::ofstream(output/"fragment.glsl") << shaders::GLSLFragment();
    }
    if (ParseShaderPreset("lcd3x")!=ShaderPreset::LCD3x ||
        ParseShaderPreset("lcd-grid-v2")!=ShaderPreset::LCDGridV2 ||
        ParseShaderPreset("broken")!=ShaderPreset::Off || IsShaderPreset("broken"))
        throw std::runtime_error("Shader name mapping failed");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    auto vcode=Compile(shaders::HLSLVertex,sizeof(shaders::HLSLVertex)-1,"vs","vs_4_0");
    const auto fragment=shaders::HLSLFragment();
    auto pcode=Compile(fragment.data(),fragment.size(),"ps","ps_4_0");
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    Check(device->CreateVertexShader(vcode->GetBufferPointer(),vcode->GetBufferSize(),nullptr,&vertex));
    Check(device->CreatePixelShader(pcode->GetBufferPointer(),pcode->GetBufferSize(),nullptr,&pixel));
    Frame frame;
    for (int y=0;y<Height;++y) for (int x=0;x<Width;++x)
        frame[y*Width+x]=0xff000000u | (unsigned(x*255/(Width-1))) |
                         (unsigned(y*255/(Height-1))<<8) | (unsigned(((x/8+y/8)&1)*255)<<16);
    if (!output.empty()) std::ofstream(output/"input.rgba",std::ios::binary).write(reinterpret_cast<const char*>(frame.data()),sizeof(frame));
    D3D11_TEXTURE2D_DESC td{};
    td.Width=Width;td.Height=Height;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{frame.data(),Width*4,0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> resource;
    Check(device->CreateTexture2D(&td,&initial,&texture));
    Check(device->CreateShaderResourceView(texture.Get(),nullptr,&resource));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(shaders::Uniforms);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    Check(device->CreateBuffer(&bd,nullptr,&constants));
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    Check(device->CreateSamplerState(&sd,&sampler));
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
    auto* r=resource.Get();context->PSSetShaderResources(0,1,&r);
    auto* b=constants.Get();context->PSSetConstantBuffers(0,1,&b);
    auto* s=sampler.Get();context->PSSetSamplers(0,1,&s);
    int cases=0,maxDifference=0;
    for (const int width:{144,240,480,720,960,333}) {
        const int height=width*2/3;
        td.Width=width;td.Height=height;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target,readback;
        ComPtr<ID3D11RenderTargetView> view;
        Check(device->CreateTexture2D(&td,nullptr,&target));
        Check(device->CreateRenderTargetView(target.Get(),nullptr,&view));
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device->CreateTexture2D(&td,nullptr,&readback));
        td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;
        D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&vp);
        auto* t=view.Get();context->OMSetRenderTargets(1,&t,nullptr);
        // Return to off after both presets, exercising updates on the same program/buffer.
        for (const int mode:{0,1,2,0}) {
            shaders::Uniforms u{float(Width),float(Height),float(width),float(height),mode};
            context->UpdateSubresource(constants.Get(),0,nullptr,&u,0,0);
            context->Draw(3,0);context->CopyResource(readback.Get(),target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
            std::vector<unsigned char> pixels(size_t(width)*height*4);
            for (int y=0;y<height;++y) std::copy_n(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,width*4,pixels.data()+y*width*4);
            context->Unmap(readback.Get(),0);
            // Corners, edge texels and a dense sample throughout the image.
            for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
                if (x>1 && x<width-2 && y>1 && y<height-2 && (x%7 || y%7)) continue;
                // At an exact nearest-sampling texel boundary either adjacent texel
                // is legal after rasterizer interpolation rounding. Grid mode uses
                // explicit texel fetches and remains checked at these coordinates.
                const double sx=(x+.5)*Width/width, sy=(y+.5)*Height/height;
                if (mode<2 && (std::abs(sx-std::round(sx))<1e-6 || std::abs(sy-std::round(sy))<1e-6)) continue;
                for (int c=0;c<3;++c) {
                    int expected=int(std::lround(std::clamp(Reference(frame,x,y,width,height,c,mode),0.,1.)*255));
                    int actual=pixels[(y*width+x)*4+c];
                    int diff=std::abs(expected-actual);maxDifference=std::max(maxDifference,diff);
                    if (diff>2) throw std::runtime_error("Pixel mismatch: width="+std::to_string(width)+" mode="+std::to_string(mode)+
                        " x="+std::to_string(x)+" y="+std::to_string(y)+" channel="+std::to_string(c)+
                        " expected="+std::to_string(expected)+" actual="+std::to_string(actual));
                }
                if (pixels[(y*width+x)*4+3]!=255) throw std::runtime_error("Output must be opaque");
            }
            if (!output.empty()) std::ofstream(output/(std::to_string(width)+"-"+std::to_string(mode)+".rgba"),std::ios::binary)
                .write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
            ++cases;
        }
    }
    std::cout<<"PASS: HLSL compile and "<<cases<<" WARP render cases vs CPU reference; maximum channel error "<<maxDifference<<"/255\n";
    return 0;
} catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
