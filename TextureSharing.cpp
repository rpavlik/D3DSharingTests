#include "Header.h"

#include <iostream>
#include <Unknwn.h>
#include <winrt/base.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3d11_4.h>

#include "d3d_dxgi_formats.h"

using namespace winrt;

constexpr UINT64 TextureWidth = 32;
constexpr UINT64 TextureHeight = 32;
constexpr UINT16 ArraySize = 1;
constexpr UINT16 MipLevels = 1;
constexpr UINT SampleCount = 1;

handle CreateD3D12Texture(com_ptr<ID3D12Device> d3d12Device, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
	com_ptr<ID3D12Resource> texture12;
	D3D12_RESOURCE_DESC textureDesc{ D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, TextureWidth, TextureHeight, ArraySize, MipLevels, format, { SampleCount, 0 }, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags };
	D3D12_HEAP_PROPERTIES heapProp = { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1 };
	check_hresult(d3d12Device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_SHARED, &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture12)));

	handle textureHandle;
	check_hresult(d3d12Device->CreateSharedHandle(texture12.get(), nullptr, GENERIC_ALL, nullptr, textureHandle.put()));

	return textureHandle;
}

HANDLE CreateD3D11Texture(com_ptr<ID3D11Device5> d3d11Device, DXGI_FORMAT format, UINT bind, UINT miscFlags) {
	assert((miscFlags & D3D11_RESOURCE_MISC_SHARED) || (miscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)); // Testing sharing

	const CD3D11_TEXTURE2D_DESC desc11{ format,
									 TextureWidth,
									 TextureHeight,
									 ArraySize,
									 MipLevels,
									 bind,
									 D3D11_USAGE_DEFAULT,
									 0 /* cpu flags */,
									 SampleCount,
									 0 /* sampleQuality */,
									 miscFlags };

	com_ptr<ID3D11Texture2D> texture11;
	check_hresult(d3d11Device->CreateTexture2D(&desc11, nullptr, texture11.put()));

	HANDLE texture11Handle{}; // winrt::handle is not used because non-NT handle isn't a real handle.
	if (desc11.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) {
		check_hresult(texture11.as<IDXGIResource1>()->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &texture11Handle));
	}
	else {
		check_hresult(texture11.as<IDXGIResource1>()->GetSharedHandle(&texture11Handle));
	}

	return texture11Handle;
}

com_ptr<ID3D12Resource> OpenSharedTextureForD3D12(com_ptr<ID3D12Device> d3d12Device, HANDLE textureHandle)
{
	com_ptr<ID3D12Resource> texture12;
	check_hresult(d3d12Device->OpenSharedHandle(textureHandle, IID_PPV_ARGS(&texture12)));
	return texture12;
}

com_ptr<ID3D11Texture2D> OpenSharedTextureForD3D11(com_ptr<ID3D11Device5> d3d11Device, HANDLE textureHandle, bool isNtHandle)
{
	com_ptr<ID3D11Texture2D> texture11;
	if (isNtHandle) {
		check_hresult(d3d11Device->OpenSharedResource1(textureHandle, IID_PPV_ARGS(&texture11))); \
	}
	else {
		check_hresult(d3d11Device->OpenSharedResource(textureHandle, IID_PPV_ARGS(&texture11)));
	}
	return texture11;
}

vk::UniqueHandle<vk::Image, vk::DispatchLoaderDynamic> OpenSharedTextureForVulkan(VulkanStuff const& vkStuff, HANDLE textureHandle, VkFormat format, bool isNtHandle)
{
	vk::ImageUsageFlags image_usage = {};

	vk::ExternalMemoryHandleTypeFlagBits handle_type = isNtHandle ? vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 : vk::ExternalMemoryHandleTypeFlagBits::eD3D11Texture;

	vk::ExternalMemoryImageCreateInfo ext_mem_img_create_info(handle_type);
	auto queueFamilyIndices = { vkStuff.queueFamilyIndex };
	auto vk_info = vk::ImageCreateInfo(
		vk::ImageCreateFlags{},
		vk::ImageType::e2D,
		format,
		vk::Extent3D(TextureWidth, TextureHeight, 1),
		MipLevels,
		ArraySize,
		vk::SampleCountFlagBits::e1,
		vk::ImageTiling::eOptimal, // TODO ?
		image_usage,
		vk::SharingMode::eExclusive, // TODO ?
		queueFamilyIndices,
		vk::ImageLayout::eUndefined, &ext_mem_img_create_info

	);

	vk::UniqueHandle<vk::Image, vk::DispatchLoaderDynamic> image;
	try {
		 image = vkStuff.device->createImageUnique(vk_info, nullptr, vkStuff.dld);

	}
	catch  (...) {
		return {};
	}
	auto importMemInfo = vk::ImportMemoryWin32HandleInfoKHR{ handle_type, textureHandle	};
	auto dedicatedMemInfo = vk::MemoryDedicatedAllocateInfo{ image.get(), nullptr, &importMemInfo };
	auto memReq = vkStuff.device->getImageMemoryRequirements(image.get(), vkStuff.dld);

	VkMemoryRequirements memory_requirements;
	vk->vkGetImageMemoryRequirements(vk->device, image, &memory_requirements);

	if (max_size > 0 && memory_requirements.size > max_size) {
		VK_ERROR(vk, "(%s) vkGetImageMemoryRequirements: Requested more memory (%u) then given (%u)\n",
			caller_name, (uint32_t)memory_requirements.size, (uint32_t)max_size);
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	if (out_size != NULL) {
		*out_size = memory_requirements.size;
	}

	uint32_t memory_type_index = UINT32_MAX;
	bool bret = vk_get_memory_type(          //
		vk,                                  // vk_bundle
		memory_requirements.memoryTypeBits,  // type_bits
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // memory_props
		&memory_type_index);                 // out_type_id
	if (!bret) {
		VK_ERROR(vk, "(%s) vk_get_memory_type: false\n\tFailed to find a matching memory type.", caller_name);
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	VkMemoryAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = pNext_for_allocate,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = memory_type_index,
	};

	VkDeviceMemory device_memory = VK_NULL_HANDLE;
	VkResult ret = vk->vkAllocateMemory(vk->device, &alloc_info, NULL, &device_memory);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "(%s) vkAllocateMemory: %s", caller_name, vk_result_string(ret));
		return ret;
	}

	// Bind the memory to the image.
	ret = vk->vkBindImageMemory(vk->device, image, device_memory, 0);
	if (ret != VK_SUCCESS) {
		// Clean up memory
		vk->vkFreeMemory(vk->device, device_memory, NULL);
		VK_ERROR(vk, "(%s) vkBindImageMemory: %s", caller_name, vk_result_string(ret));
		return ret;
	}
}
template <typename TInt>
struct StrValue
{
	TInt Value;
	const char* Str;

	StrValue(TInt v, const char* str) : Value(v), Str(str) {}
};

#define MAKE_VAL(x) StrValue(x, #x)
#define MAKE_VAL2(t, x) StrValue<t>((t)(x), #x)

void TexturePermationSharingTests(com_ptr<ID3D11Device5> d3d11Device, com_ptr<ID3D11Device5> d3d11DeviceSecond, com_ptr<ID3D12Device> d3d12Device, com_ptr<ID3D12Device> d3d12DeviceSecond,
	vk::Device vulkanDevice, const vk::DispatchLoaderDynamic& dld, bool skipFailedAllocations) {
	const StrValue<DXGI_FORMAT> formats[] = {
MAKE_VAL(DXGI_FORMAT_R24G8_TYPELESS),
MAKE_VAL(DXGI_FORMAT_D24_UNORM_S8_UINT),
MAKE_VAL(DXGI_FORMAT_R24_UNORM_X8_TYPELESS),

MAKE_VAL(DXGI_FORMAT_X24_TYPELESS_G8_UINT),
MAKE_VAL(DXGI_FORMAT_D32_FLOAT_S8X24_UINT),
MAKE_VAL(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS),
MAKE_VAL(DXGI_FORMAT_X32_TYPELESS_G8X24_UINT),

MAKE_VAL(DXGI_FORMAT_R32_TYPELESS),
MAKE_VAL(DXGI_FORMAT_D32_FLOAT),
MAKE_VAL(DXGI_FORMAT_R32_FLOAT),
MAKE_VAL(DXGI_FORMAT_R32_UINT),
MAKE_VAL(DXGI_FORMAT_R32_SINT),

MAKE_VAL(DXGI_FORMAT_R16_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R16_FLOAT),
MAKE_VAL(DXGI_FORMAT_D16_UNORM),
MAKE_VAL(DXGI_FORMAT_R16_UNORM),
MAKE_VAL(DXGI_FORMAT_R16_UINT),
MAKE_VAL(DXGI_FORMAT_R16_SNORM),
MAKE_VAL(DXGI_FORMAT_R16_SINT),

MAKE_VAL(DXGI_FORMAT_R8G8B8A8_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R8G8B8A8_UNORM),
MAKE_VAL(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
MAKE_VAL(DXGI_FORMAT_R8G8B8A8_UINT),
MAKE_VAL(DXGI_FORMAT_R8G8B8A8_SNORM),
MAKE_VAL(DXGI_FORMAT_R8G8B8A8_SINT),
MAKE_VAL(DXGI_FORMAT_B8G8R8A8_UNORM),
MAKE_VAL(DXGI_FORMAT_B8G8R8X8_UNORM),
MAKE_VAL(DXGI_FORMAT_B8G8R8A8_TYPELESS),
MAKE_VAL(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),
MAKE_VAL(DXGI_FORMAT_B8G8R8X8_TYPELESS),
MAKE_VAL(DXGI_FORMAT_B8G8R8X8_UNORM_SRGB),

MAKE_VAL(DXGI_FORMAT_R10G10B10A2_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R10G10B10A2_UNORM),
MAKE_VAL(DXGI_FORMAT_R10G10B10A2_UINT),
MAKE_VAL(DXGI_FORMAT_R11G11B10_FLOAT),

MAKE_VAL(DXGI_FORMAT_R16G16B16A16_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R16G16B16A16_FLOAT),
MAKE_VAL(DXGI_FORMAT_R16G16B16A16_UNORM),
MAKE_VAL(DXGI_FORMAT_R16G16B16A16_UINT),
MAKE_VAL(DXGI_FORMAT_R16G16B16A16_SNORM),
MAKE_VAL(DXGI_FORMAT_R16G16B16A16_SINT),

/*
// Less common formats excluded by default
MAKE_VAL(DXGI_FORMAT_NV11),
MAKE_VAL(DXGI_FORMAT_NV12),
MAKE_VAL(DXGI_FORMAT_R32G32B32A32_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R32G32B32A32_FLOAT),
MAKE_VAL(DXGI_FORMAT_R32G32B32A32_UINT),
MAKE_VAL(DXGI_FORMAT_R32G32B32A32_SINT),
MAKE_VAL(DXGI_FORMAT_R32G32B32_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R32G32B32_FLOAT),
MAKE_VAL(DXGI_FORMAT_R32G32B32_UINT),
MAKE_VAL(DXGI_FORMAT_R32G32B32_SINT),
MAKE_VAL(DXGI_FORMAT_R32G32_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R32G32_FLOAT),
MAKE_VAL(DXGI_FORMAT_R32G32_UINT),
MAKE_VAL(DXGI_FORMAT_R32G32_SINT),
MAKE_VAL(DXGI_FORMAT_R32G8X24_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R16G16_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R16G16_FLOAT),
MAKE_VAL(DXGI_FORMAT_R16G16_UNORM),
MAKE_VAL(DXGI_FORMAT_R16G16_UINT),
MAKE_VAL(DXGI_FORMAT_R16G16_SNORM),
MAKE_VAL(DXGI_FORMAT_R16G16_SINT),
MAKE_VAL(DXGI_FORMAT_R8G8_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R8G8_UNORM),
MAKE_VAL(DXGI_FORMAT_R8G8_UINT),
MAKE_VAL(DXGI_FORMAT_R8G8_SNORM),
MAKE_VAL(DXGI_FORMAT_R8G8_SINT),
MAKE_VAL(DXGI_FORMAT_R8_TYPELESS),
MAKE_VAL(DXGI_FORMAT_R8_UNORM),
MAKE_VAL(DXGI_FORMAT_R8_UINT),
MAKE_VAL(DXGI_FORMAT_R8_SNORM),
MAKE_VAL(DXGI_FORMAT_R8_SINT),
MAKE_VAL(DXGI_FORMAT_A8_UNORM),
MAKE_VAL(DXGI_FORMAT_R1_UNORM),
MAKE_VAL(DXGI_FORMAT_R9G9B9E5_SHAREDEXP),
MAKE_VAL(DXGI_FORMAT_R8G8_B8G8_UNORM),
MAKE_VAL(DXGI_FORMAT_G8R8_G8B8_UNORM),
MAKE_VAL(DXGI_FORMAT_BC1_TYPELESS),
MAKE_VAL(DXGI_FORMAT_BC1_UNORM),
MAKE_VAL(DXGI_FORMAT_BC1_UNORM_SRGB),
MAKE_VAL(DXGI_FORMAT_BC2_TYPELESS),
MAKE_VAL(DXGI_FORMAT_BC2_UNORM),
MAKE_VAL(DXGI_FORMAT_BC2_UNORM_SRGB),
MAKE_VAL(DXGI_FORMAT_BC3_TYPELESS),
MAKE_VAL(DXGI_FORMAT_BC3_UNORM),
MAKE_VAL(DXGI_FORMAT_BC3_UNORM_SRGB),
MAKE_VAL(DXGI_FORMAT_BC4_TYPELESS),
MAKE_VAL(DXGI_FORMAT_BC4_UNORM),
MAKE_VAL(DXGI_FORMAT_BC4_SNORM),
MAKE_VAL(DXGI_FORMAT_BC5_TYPELESS),
MAKE_VAL(DXGI_FORMAT_BC5_UNORM),
MAKE_VAL(DXGI_FORMAT_BC5_SNORM),
MAKE_VAL(DXGI_FORMAT_B5G6R5_UNORM),
MAKE_VAL(DXGI_FORMAT_B5G5R5A1_UNORM),
MAKE_VAL(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM),
MAKE_VAL(DXGI_FORMAT_BC6H_TYPELESS),
MAKE_VAL(DXGI_FORMAT_BC6H_UF16),
MAKE_VAL(DXGI_FORMAT_BC6H_SF16),
MAKE_VAL(DXGI_FORMAT_BC7_TYPELESS),
MAKE_VAL(DXGI_FORMAT_BC7_UNORM),
MAKE_VAL(DXGI_FORMAT_BC7_UNORM_SRGB),
MAKE_VAL(DXGI_FORMAT_AYUV),
MAKE_VAL(DXGI_FORMAT_Y410),
MAKE_VAL(DXGI_FORMAT_Y416),
MAKE_VAL(DXGI_FORMAT_P010),
MAKE_VAL(DXGI_FORMAT_P016),
MAKE_VAL(DXGI_FORMAT_420_OPAQUE),
MAKE_VAL(DXGI_FORMAT_YUY2),
MAKE_VAL(DXGI_FORMAT_Y210),
MAKE_VAL(DXGI_FORMAT_Y216),
MAKE_VAL(DXGI_FORMAT_AI44),
MAKE_VAL(DXGI_FORMAT_IA44),
MAKE_VAL(DXGI_FORMAT_P8),
MAKE_VAL(DXGI_FORMAT_A8P8),
MAKE_VAL(DXGI_FORMAT_B4G4R4A4_UNORM),
MAKE_VAL(DXGI_FORMAT_P208),
MAKE_VAL(DXGI_FORMAT_V208),
MAKE_VAL(DXGI_FORMAT_V408)
*/
	};

	const StrValue<D3D11_BIND_FLAG> bindFlags11[] = {
		MAKE_VAL(D3D11_BIND_DEPTH_STENCIL),
		MAKE_VAL(D3D11_BIND_RENDER_TARGET),
		MAKE_VAL(D3D11_BIND_SHADER_RESOURCE)
	};

	const StrValue<D3D11_RESOURCE_MISC_FLAG> miscFlags11[] = {
		MAKE_VAL(D3D11_RESOURCE_MISC_SHARED),
		MAKE_VAL2(D3D11_RESOURCE_MISC_FLAG, D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE),
		MAKE_VAL2(D3D11_RESOURCE_MISC_FLAG, D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX),
		MAKE_VAL2(D3D11_RESOURCE_MISC_FLAG, D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX),
	};

	const StrValue<D3D12_RESOURCE_FLAGS> resourceFlags12[] = {
		MAKE_VAL(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		MAKE_VAL(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),
		MAKE_VAL(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
		MAKE_VAL(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
	};

	for (auto format : formats) {
		printf("Format %s\n", format.Str);

		VkFormat vkformat = (VkFormat)d3d_dxgi_format_to_vk(format.Value);

		// 11 to 11
		printf("  D3D11 shared to D3D11:\n");
		bool textureCreated = false;
		for (auto bindFlag : bindFlags11) {
			for (auto miscFlag : miscFlags11) {
				HANDLE texture11{};
				handle smartHandle;
				bool isNtHandle;

				const char* result = "Unknown";

				// Create the D3D11 texture and share to a handle
				try {
					texture11 = CreateD3D11Texture(d3d11Device, format.Value, bindFlag.Value, miscFlag.Value);
					isNtHandle = (miscFlag.Value & D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
					if (isNtHandle) {
						smartHandle.attach(texture11); // Ensure CloseHandle is called on NT handles.
					}
					textureCreated = true;
				}
				catch (...) {
					result = "D3D11 Texture not created";
					if (skipFailedAllocations) continue; // No point in displaying data for invalid texture configuration.
				}

				if (texture11) {
					try {
						auto texture11b = OpenSharedTextureForD3D11(d3d11Device, texture11, isNtHandle);
						result = "Success";
					}
					catch (...) {
					}
				}

				printf("    Bind=%-26s Misc=%-80s = %s\n", bindFlag.Str, miscFlag.Str, result);
			}
		}
		if (!textureCreated) {
			printf("    Cannot create source texture\n"); // None of the flag permutations are correct for this format.
		}

		// 11 to 12
		printf("  D3D11 shared to D3D12:\n");
		textureCreated = false;
		for (auto bindFlag : bindFlags11) {
			for (auto miscFlag : miscFlags11) {
				HANDLE texture11{};
				handle smartHandle;

				const char* result = "Unknown";

				try {
					texture11 = CreateD3D11Texture(d3d11Device, format.Value, bindFlag.Value, miscFlag.Value);
					if (miscFlag.Value & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) {
						smartHandle.attach(texture11);
					}
					textureCreated = true;
				}
				catch (...) {
					result = "D3D11 Texture not created";
					if (skipFailedAllocations) continue; // No point in displaying data for invalid texture configuration.
				}

				if (texture11) {
					try {
						auto texture12 = OpenSharedTextureForD3D12(d3d12Device, texture11);
						result = (texture12->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) ? "Success (ALLOW_SIMULTANEOUS_ACCESS)" : "Success";
					}
					catch (...) {
						result = "Failed";
					}
				}

				printf("    Bind=%-26s Misc=%-80s = %s\n", bindFlag.Str, miscFlag.Str, result);
			}
		}
		if (!textureCreated) {
			printf("    Cannot create source texture\n"); // None of the flag permutations are correct for this format.
		}

		// 12 to 11
		printf("  D3D12 shared to D3D11:\n");
		textureCreated = false;
		for (auto resourceFlag : resourceFlags12) {
			handle texture12;

			const char* result = "Unknown";

			try {
				texture12 = CreateD3D12Texture(d3d12Device, format.Value, resourceFlag.Value);
				textureCreated = true;
			}
			catch (...) {
				result = "D3D12 Texture not created";
				if (skipFailedAllocations) continue; // No point in displaying data for invalid texture configuration.
			}

			if (texture12) {
				try {
					auto texture11 = OpenSharedTextureForD3D11(d3d11Device, texture12.get(), true /* d3d12 is always nt handle */);
					result = "Success";
				}
				catch (...) {
					result = "Failed";
				}
				printf("    ResourceFlag=%-104s = %s\n", resourceFlag.Str, result);
			}
		}
		if (!textureCreated) {
			printf("    Cannot create source texture\n"); // None of the flag permutations are correct for this format.
		}

		// 12 to 12
		printf("  D3D12 shared to D3D12:\n");
		textureCreated = false;
		for (auto resourceFlag : resourceFlags12) {
			handle texture12;

			const char* result = "Unknown";

			try {
				texture12 = CreateD3D12Texture(d3d12Device, format.Value, resourceFlag.Value);
				textureCreated = true;
			}
			catch (...) {
				result = "D3D12 Texture not created";
				if (skipFailedAllocations) continue; // No point in displaying data for invalid texture configuration.
			}

			if (texture12) {
				try {
					auto texture12obj = OpenSharedTextureForD3D12(d3d12Device, texture12.get());
					result = "Success";
				}
				catch (...) {
					result = "Failed";
				}
				printf("    ResourceFlag=%-104s = %s\n", resourceFlag.Str, result);
			}
		}
		if (!textureCreated) {
			printf("    Cannot create source texture\n"); // None of the flag permutations are correct for this format.
		}

		printf("\n");
	}
}
