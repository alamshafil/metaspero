#ifndef EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_METALFXSCALER_H_
#define EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_METALFXSCALER_H_

#include "common/common.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <memory>

namespace Libs::Graphics {

// Apple Silicon MetalFX Spatial upscaler used during presentation when the game frame is smaller
// than the swapchain viewport (e.g. 50% / 1080p -> 4K). No-op on non-Apple platforms.
class MetalFxSpatialScaler final {
public:
	explicit MetalFxSpatialScaler(GraphicContext& graphics);
	~MetalFxSpatialScaler();
	KYTY_CLASS_NO_COPY(MetalFxSpatialScaler);

	[[nodiscard]] bool IsAvailable() const noexcept;

	// Encodes MetalFX Spatial upscaling from src_image (src_extent) into an internal output image
	// of dst_extent on the graphics queue's underlying MTLCommandQueue. Returns true and sets
	// out_image when upscaling succeeded, returns false to fall back to standard Vulkan blit.
	[[nodiscard]] bool Upscale(vk::Image src_image, vk::Extent2D src_extent, vk::Format format,
	                           bool hdr, vk::Extent2D dst_extent, VulkanImage*& out_image);

	void Reset();

private:
#if defined(__APPLE__)
	struct Impl;
	std::unique_ptr<Impl> m_impl;
#endif
};

#if !defined(__APPLE__)
inline MetalFxSpatialScaler::MetalFxSpatialScaler(GraphicContext&) {}
inline MetalFxSpatialScaler::~MetalFxSpatialScaler() = default;
inline bool MetalFxSpatialScaler::IsAvailable() const noexcept { return false; }
inline bool MetalFxSpatialScaler::Upscale(vk::Image, vk::Extent2D, vk::Format, bool, vk::Extent2D,
                                          VulkanImage*&) {
	return false;
}
inline void MetalFxSpatialScaler::Reset() {}
#endif

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_METALFXSCALER_H_
