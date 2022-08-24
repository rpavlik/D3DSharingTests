#pragma once
#include <d3d11_4.h>
#include <d3d12.h>
#include <winrt/base.h>

// TextureSharing.cpp
void TexturePermationSharingTests(winrt::com_ptr<ID3D11Device5> d3d11Device, winrt::com_ptr<ID3D11Device5> d3d11DeviceSecond, winrt::com_ptr<ID3D12Device> d3d12Device, winrt::com_ptr<ID3D12Device> d3d12DeviceSecond);

// FenceSharing.cpp
void Fence11To12Test(winrt::com_ptr<ID3D11Device5> d3d11Device, winrt::com_ptr<ID3D12Device> d3d12Device);
void Fence12To11Test(winrt::com_ptr<ID3D11Device5> d3d11Device, winrt::com_ptr<ID3D12Device> d3d12Device);
void Fence12To12Test(winrt::com_ptr<ID3D12Device> d3d12Device1, winrt::com_ptr<ID3D12Device> d3d12Device2);
