#if defined(__APPLE__)

#include "graphics/presentation/window/metalFxScaler.h"

#include "common/logging/log.h"

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include <vulkan/vulkan_metal.h>

namespace Libs::Graphics {

struct MetalFxSpatialScaler::Impl {
	GraphicContext&           graphics;
	PFN_vkExportMetalObjectsEXT export_metal_objects = nullptr;
	id<MTLDevice>             mtl_device             = nil;
	id<MTLCommandQueue>       mtl_queue              = nil;
	id<MTLFXSpatialScaler>    scaler                 = nil;
	VulkanImage               output {};
	vk::Extent2D              cached_src {};
	vk::Extent2D              cached_dst {};
	MTLPixelFormat            cached_src_fmt         = MTLPixelFormatInvalid;
	MTLPixelFormat            cached_dst_fmt         = MTLPixelFormatInvalid;
	bool                      cached_hdr             = false;
	bool                      available              = false;
	bool                      logged_active          = false;

	explicit Impl(GraphicContext& g): graphics(g) {
		if (!graphics.metal_objects_ext_enabled || graphics.device == nullptr ||
		    graphics.queue == nullptr) {
			return;
		}
		export_metal_objects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(
		    graphics.device.getProcAddr("vkExportMetalObjectsEXT"));
		if (export_metal_objects == nullptr) {
			return;
		}

		VkExportMetalDeviceInfoEXT dev_info {};
		dev_info.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT;

		VkExportMetalCommandQueueInfoEXT queue_info {};
		queue_info.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_COMMAND_QUEUE_INFO_EXT;
		queue_info.pNext = &dev_info;
		queue_info.queue = static_cast<VkQueue>(graphics.queue);

		VkExportMetalObjectsInfoEXT objects_info {};
		objects_info.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT;
		objects_info.pNext = &queue_info;

		export_metal_objects(static_cast<VkDevice>(graphics.device), &objects_info);
		mtl_device = dev_info.mtlDevice;
		mtl_queue  = queue_info.mtlCommandQueue;
		if (mtl_device == nil || mtl_queue == nil) {
			return;
		}
		if (@available(macOS 13.0, iOS 16.0, *)) {
			available = [MTLFXSpatialScalerDescriptor supportsDevice:mtl_device];
		}
		LOGF("MetalFX Spatial upscaler: %s\n", available ? "available" : "unavailable");
	}

	~Impl() { Reset(); }

	void Reset() {
		scaler         = nil;
		cached_src     = {};
		cached_dst     = {};
		cached_src_fmt = MTLPixelFormatInvalid;
		cached_dst_fmt = MTLPixelFormatInvalid;
		if (output.image != nullptr) {
			graphics.WaitQueueIdle();
			graphics.DeleteImage(output);
		}
	}

	id<MTLTexture> ExportTexture(vk::Image image) const {
		VkExportMetalTextureInfoEXT tex_info {};
		tex_info.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT;
		tex_info.image = static_cast<VkImage>(image);
		tex_info.plane = VK_IMAGE_ASPECT_COLOR_BIT;

		VkExportMetalObjectsInfoEXT objects_info {};
		objects_info.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT;
		objects_info.pNext = &tex_info;

		export_metal_objects(static_cast<VkDevice>(graphics.device), &objects_info);
		return tex_info.mtlTexture;
	}

	bool EnsureOutput(vk::Extent2D dst_extent, vk::Format format) {
		if (output.image != nullptr && output.extent.width == dst_extent.width &&
		    output.extent.height == dst_extent.height && output.format == format) {
			return true;
		}
		if (output.image != nullptr) {
			graphics.WaitQueueIdle();
			graphics.DeleteImage(output);
		}
		VkExportMetalObjectCreateInfoEXT export_create {};
		export_create.sType            = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT;
		export_create.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;

		vk::ImageCreateInfo create {};
		create.sType         = vk::StructureType::eImageCreateInfo;
		create.pNext         = &export_create;
		create.imageType     = vk::ImageType::e2D;
		create.extent        = {dst_extent.width, dst_extent.height, 1};
		create.mipLevels     = 1;
		create.arrayLayers   = 1;
		create.format        = format;
		create.tiling        = vk::ImageTiling::eOptimal;
		create.initialLayout = vk::ImageLayout::eUndefined;
		create.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage |
		               vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
		create.sharingMode = vk::SharingMode::eExclusive;
		create.samples     = vk::SampleCountFlagBits::e1;
		return graphics.CreateImage(create, output);
	}

	bool Upscale(vk::Image src_image, vk::Extent2D src_extent, vk::Format format, bool hdr,
	             vk::Extent2D dst_extent, VulkanImage*& out_image) {
		if (!available || src_image == nullptr) {
			return false;
		}
		// Only run MetalFX Spatial when actually upscaling.
		if (src_extent.width >= dst_extent.width && src_extent.height >= dst_extent.height) {
			return false;
		}
		if (src_extent.width == 0 || src_extent.height == 0 || dst_extent.width == 0 ||
		    dst_extent.height == 0) {
			return false;
		}
		if (!EnsureOutput(dst_extent, format)) {
			return false;
		}

		@autoreleasepool {
			id<MTLTexture> src_tex = ExportTexture(src_image);
			id<MTLTexture> dst_tex = ExportTexture(output.image);
			if (src_tex == nil || dst_tex == nil) {
				return false;
			}
			const MTLPixelFormat src_fmt = src_tex.pixelFormat;
			const MTLPixelFormat dst_fmt = dst_tex.pixelFormat;
			if (scaler == nil || cached_src != src_extent || cached_dst != dst_extent ||
			    cached_src_fmt != src_fmt || cached_dst_fmt != dst_fmt || cached_hdr != hdr) {
				if (@available(macOS 13.0, iOS 16.0, *)) {
					MTLFXSpatialScalerDescriptor* desc = [MTLFXSpatialScalerDescriptor new];
					desc.inputWidth                    = src_extent.width;
					desc.inputHeight                   = src_extent.height;
					desc.outputWidth                   = dst_extent.width;
					desc.outputHeight                  = dst_extent.height;
					desc.colorTextureFormat            = src_fmt;
					desc.outputTextureFormat           = dst_fmt;
					desc.colorProcessingMode =
					    hdr ? MTLFXSpatialScalerColorProcessingModeHDR
					        : MTLFXSpatialScalerColorProcessingModePerceptual;
					scaler = [desc newSpatialScalerWithDevice:mtl_device];
				}
				if (scaler == nil) {
					return false;
				}
				cached_src     = src_extent;
				cached_dst     = dst_extent;
				cached_src_fmt = src_fmt;
				cached_dst_fmt = dst_fmt;
				cached_hdr     = hdr;
				if (!logged_active) {
					logged_active = true;
					LOGF("MetalFX Spatial active: %ux%u -> %ux%u (%s)\n", src_extent.width,
					     src_extent.height, dst_extent.width, dst_extent.height,
					     hdr ? "HDR" : "SDR");
				}
			}

			// Commit on MoltenVK's exact MTLCommandQueue under queue_mutex so Metal's FIFO queue
			// ordering guarantees CopyFrom completes before MetalFX reads src_tex, and MetalFX
			// completes before the subsequent Vulkan present blit reads dst_tex.
			Common::LockGuard queue_lock(graphics.queue_mutex);
			id<MTLCommandBuffer> cmd_buf = [mtl_queue commandBuffer];
			if (cmd_buf == nil) {
				return false;
			}
			scaler.colorTexture       = src_tex;
			scaler.outputTexture      = dst_tex;
			scaler.inputContentWidth  = src_extent.width;
			scaler.inputContentHeight = src_extent.height;
			[scaler encodeToCommandBuffer:cmd_buf];
			[cmd_buf commit];
		}

		out_image = &output;
		return true;
	}
};

MetalFxSpatialScaler::MetalFxSpatialScaler(GraphicContext& graphics)
    : m_impl(std::make_unique<Impl>(graphics)) {}

MetalFxSpatialScaler::~MetalFxSpatialScaler() = default;

bool MetalFxSpatialScaler::IsAvailable() const noexcept {
	return m_impl != nullptr && m_impl->available;
}

bool MetalFxSpatialScaler::Upscale(vk::Image src_image, vk::Extent2D src_extent, vk::Format format,
                                   bool hdr, vk::Extent2D dst_extent, VulkanImage*& out_image) {
	return m_impl != nullptr &&
	       m_impl->Upscale(src_image, src_extent, format, hdr, dst_extent, out_image);
}

void MetalFxSpatialScaler::Reset() {
	if (m_impl != nullptr) {
		m_impl->Reset();
	}
}

} // namespace Libs::Graphics

#endif // defined(__APPLE__)
