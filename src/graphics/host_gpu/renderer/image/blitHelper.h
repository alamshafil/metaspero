#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_BLITHELPER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_BLITHELPER_H_

#include "common/common.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <compare>
#include <span>
#include <vector>

namespace Libs::Graphics {

class CommandScheduler;
class Image;
struct GraphicContext;

class BlitHelper final {
public:
	inline static constexpr auto ColorToMsDepthLayout =
	    vk::ImageLayout::eDepthStencilAttachmentOptimal;

	BlitHelper(GraphicContext& graphics, CommandScheduler& scheduler);
	~BlitHelper();
	KYTY_CLASS_NO_COPY(BlitHelper);

	void ReinterpretColorAsMsDepth(Image& source, Image& destination);
	void ClearColorIndirect(vk::CommandBuffer command, vk::Format format, uint32_t samples,
	                        vk::Rect2D area, uint32_t slices, uint32_t keys,
	                        std::span<const vk::ClearAttachment> clears, vk::Buffer predicates);

private:
	struct PipelineKey {
		uint32_t   samples     = 1;
		vk::Format format      = vk::Format::eUndefined;
		bool       color_clear = false;

		auto operator<=>(const PipelineKey&) const = default;
	};

	struct Pipeline {
		PipelineKey  key;
		vk::Pipeline handle = nullptr;
	};

	[[nodiscard]] vk::Pipeline GetPipeline(PipelineKey key);

	GraphicContext&         m_graphics;
	CommandScheduler&       m_scheduler;
	vk::DescriptorSetLayout m_descriptor_layout  = nullptr;
	vk::PipelineLayout      m_pipeline_layout    = nullptr;
	vk::ShaderModule        m_vertex_shader      = nullptr;
	vk::ShaderModule        m_fragment_shader    = nullptr;
	vk::ShaderModule        m_clear_float_shader = nullptr;
	vk::ShaderModule        m_clear_uint_shader  = nullptr;
	vk::ShaderModule        m_clear_sint_shader  = nullptr;
	std::vector<Pipeline>   m_pipelines;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_BLITHELPER_H_
