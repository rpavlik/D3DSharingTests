#pragma once
#include <d3d11_4.h>
#include <d3d12.h>
#include <winrt/base.h>

#define VK_USE_PLATFORM_WIN32_KHR

#include <vulkan/vulkan.hpp>

struct VulkanStuff {
	explicit VulkanStuff(DXGI_ADAPTER_DESC desc);
	vk::DispatchLoaderDynamic dld;
	vk::UniqueInstance instance;
	vk::UniqueDevice device;
	uint32_t queueFamilyIndex;
	vk::UniqueHandle<vk::CommandPool, vk::DispatchLoaderDynamic> commandPool;
	
};

// TextureSharing.cpp
void TexturePermationSharingTests(winrt::com_ptr<ID3D11Device5> d3d11Device, winrt::com_ptr<ID3D11Device5> d3d11DeviceSecond, winrt::com_ptr<ID3D12Device> d3d12Device, winrt::com_ptr<ID3D12Device> d3d12DeviceSecond,
	VulkanStuff const& vkStuff, bool skipFailedAllocations = true);

// FenceSharing.cpp
void Fence11To12Test(winrt::com_ptr<ID3D11Device5> d3d11Device, winrt::com_ptr<ID3D12Device> d3d12Device);
void Fence12To11Test(winrt::com_ptr<ID3D11Device5> d3d11Device, winrt::com_ptr<ID3D12Device> d3d12Device);
void Fence12To12Test(winrt::com_ptr<ID3D12Device> d3d12Device1, winrt::com_ptr<ID3D12Device> d3d12Device2);
