#include "graphics/host_gpu/renderer/image/blitHelper.h"

#include "common/assert.h"
#include "gpu_blit_shaders/gpu_blit_color_to_ms_depth_spv.h"
#include "gpu_blit_shaders/gpu_blit_fs_triangle_spv.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/renderer/image/image.h"
#include "graphics/host_gpu/renderer/renderTarget.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <vulkan/vulkan_format_traits.hpp>

namespace Libs::Graphics {

namespace {

constexpr std::array<uint32_t, 127> CLEAR_FRAG_FLOAT_SPV {
    0x07230203u, 0x00010600u, 0x0008000bu, 0x00000012u, 0x00000000u, 0x00020011u, 0x00000001u,
    0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu,
    0x00000000u, 0x00000001u, 0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00000009u, 0x0000000cu, 0x00030010u, 0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u,
    0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00050005u, 0x00000009u,
    0x5f74756fu, 0x6f6c6f63u, 0x00000072u, 0x00040005u, 0x0000000au, 0x68737550u, 0x00000000u,
    0x00050006u, 0x0000000au, 0x00000000u, 0x6f6c6f63u, 0x00000072u, 0x00040005u, 0x0000000cu,
    0x68737570u, 0x00000000u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00030047u,
    0x0000000au, 0x00000002u, 0x00050048u, 0x0000000au, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u,
    0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u,
    0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u, 0x0003001eu,
    0x0000000au, 0x00000007u, 0x00040020u, 0x0000000bu, 0x00000009u, 0x0000000au, 0x0004003bu,
    0x0000000bu, 0x0000000cu, 0x00000009u, 0x00040015u, 0x0000000du, 0x00000020u, 0x00000001u,
    0x0004002bu, 0x0000000du, 0x0000000eu, 0x00000000u, 0x00040020u, 0x0000000fu, 0x00000009u,
    0x00000007u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u,
    0x00000005u, 0x00050041u, 0x0000000fu, 0x00000010u, 0x0000000cu, 0x0000000eu, 0x0004003du,
    0x00000007u, 0x00000011u, 0x00000010u, 0x0003003eu, 0x00000009u, 0x00000011u, 0x000100fdu,
    0x00010038u};

constexpr std::array<uint32_t, 128> CLEAR_FRAG_UINT_SPV {
    0x07230203u, 0x00010600u, 0x0008000bu, 0x00000012u, 0x00000000u, 0x00020011u, 0x00000001u,
    0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu,
    0x00000000u, 0x00000001u, 0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00000009u, 0x0000000cu, 0x00030010u, 0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u,
    0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00050005u, 0x00000009u,
    0x5f74756fu, 0x6f6c6f63u, 0x00000072u, 0x00040005u, 0x0000000au, 0x68737550u, 0x00000000u,
    0x00050006u, 0x0000000au, 0x00000000u, 0x6f6c6f63u, 0x00000072u, 0x00040005u, 0x0000000cu,
    0x68737570u, 0x00000000u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00030047u,
    0x0000000au, 0x00000002u, 0x00050048u, 0x0000000au, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u,
    0x00000020u, 0x00000000u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u,
    0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u,
    0x0003001eu, 0x0000000au, 0x00000007u, 0x00040020u, 0x0000000bu, 0x00000009u, 0x0000000au,
    0x0004003bu, 0x0000000bu, 0x0000000cu, 0x00000009u, 0x00040015u, 0x0000000du, 0x00000020u,
    0x00000001u, 0x0004002bu, 0x0000000du, 0x0000000eu, 0x00000000u, 0x00040020u, 0x0000000fu,
    0x00000009u, 0x00000007u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u,
    0x000200f8u, 0x00000005u, 0x00050041u, 0x0000000fu, 0x00000010u, 0x0000000cu, 0x0000000eu,
    0x0004003du, 0x00000007u, 0x00000011u, 0x00000010u, 0x0003003eu, 0x00000009u, 0x00000011u,
    0x000100fdu, 0x00010038u};

constexpr std::array<uint32_t, 124> CLEAR_FRAG_SINT_SPV {
    0x07230203u, 0x00010600u, 0x0008000bu, 0x00000011u, 0x00000000u, 0x00020011u, 0x00000001u,
    0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu,
    0x00000000u, 0x00000001u, 0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00000009u, 0x0000000cu, 0x00030010u, 0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u,
    0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00050005u, 0x00000009u,
    0x5f74756fu, 0x6f6c6f63u, 0x00000072u, 0x00040005u, 0x0000000au, 0x68737550u, 0x00000000u,
    0x00050006u, 0x0000000au, 0x00000000u, 0x6f6c6f63u, 0x00000072u, 0x00040005u, 0x0000000cu,
    0x68737570u, 0x00000000u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00030047u,
    0x0000000au, 0x00000002u, 0x00050048u, 0x0000000au, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u,
    0x00000020u, 0x00000001u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u,
    0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u,
    0x0003001eu, 0x0000000au, 0x00000007u, 0x00040020u, 0x0000000bu, 0x00000009u, 0x0000000au,
    0x0004003bu, 0x0000000bu, 0x0000000cu, 0x00000009u, 0x0004002bu, 0x00000006u, 0x0000000du,
    0x00000000u, 0x00040020u, 0x0000000eu, 0x00000009u, 0x00000007u, 0x00050036u, 0x00000002u,
    0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x00050041u, 0x0000000eu,
    0x0000000fu, 0x0000000cu, 0x0000000du, 0x0004003du, 0x00000007u, 0x00000010u, 0x0000000fu,
    0x0003003eu, 0x00000009u, 0x00000010u, 0x000100fdu, 0x00010038u};

} // namespace

BlitHelper::BlitHelper(GraphicContext& graphics, CommandScheduler& scheduler)
    : m_graphics(graphics), m_scheduler(scheduler) {
	vk::DescriptorSetLayoutBinding texture_binding {};
	texture_binding.binding         = 0;
	texture_binding.descriptorType  = vk::DescriptorType::eSampledImage;
	texture_binding.descriptorCount = 1;
	texture_binding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

	vk::DescriptorSetLayoutCreateInfo descriptor_info {};
	descriptor_info.flags        = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
	descriptor_info.bindingCount = 1;
	descriptor_info.pBindings    = &texture_binding;
	RequireVulkanSuccess(m_graphics.device.createDescriptorSetLayout(&descriptor_info, nullptr,
	                                                                 &m_descriptor_layout),
	                     "create BlitHelper descriptor layout");

	vk::PushConstantRange push_constant {};
	push_constant.stageFlags = vk::ShaderStageFlagBits::eFragment;
	push_constant.offset     = 0;
	push_constant.size       = sizeof(vk::ClearColorValue);

	vk::PipelineLayoutCreateInfo layout_info {};
	layout_info.setLayoutCount         = 1;
	layout_info.pSetLayouts            = &m_descriptor_layout;
	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges    = &push_constant;
	RequireVulkanSuccess(
	    m_graphics.device.createPipelineLayout(&layout_info, nullptr, &m_pipeline_layout),
	    "create BlitHelper pipeline layout");

	m_vertex_shader      = CompileSPV(GPU_BLIT_FS_TRIANGLE_SPV, m_graphics.device);
	m_fragment_shader    = CompileSPV(GPU_BLIT_COLOR_TO_MS_DEPTH_SPV, m_graphics.device);
	m_clear_float_shader = CompileSPV(CLEAR_FRAG_FLOAT_SPV, m_graphics.device);
	m_clear_uint_shader  = CompileSPV(CLEAR_FRAG_UINT_SPV, m_graphics.device);
	m_clear_sint_shader  = CompileSPV(CLEAR_FRAG_SINT_SPV, m_graphics.device);
}

BlitHelper::~BlitHelper() {
	for (const auto& pipeline: m_pipelines) {
		m_graphics.device.destroyPipeline(pipeline.handle, nullptr);
	}
	m_graphics.device.destroyShaderModule(m_clear_sint_shader, nullptr);
	m_graphics.device.destroyShaderModule(m_clear_uint_shader, nullptr);
	m_graphics.device.destroyShaderModule(m_clear_float_shader, nullptr);
	m_graphics.device.destroyShaderModule(m_fragment_shader, nullptr);
	m_graphics.device.destroyShaderModule(m_vertex_shader, nullptr);
	m_graphics.device.destroyPipelineLayout(m_pipeline_layout, nullptr);
	m_graphics.device.destroyDescriptorSetLayout(m_descriptor_layout, nullptr);
}

vk::Pipeline BlitHelper::GetPipeline(PipelineKey key) {
	const auto cached = std::ranges::find(m_pipelines, key, &Pipeline::key);
	if (cached != m_pipelines.end()) {
		return cached->handle;
	}

	const auto samples = vulkan_sample_count(key.samples);
	EXIT_IF(samples == vk::SampleCountFlagBits {} || key.format == vk::Format::eUndefined);

	vk::ShaderModule fragment_module = m_fragment_shader;
	if (key.color_clear) {
		const std::string_view numeric = vk::componentNumericFormat(key.format, 0);
		if (numeric == "UINT") {
			fragment_module = m_clear_uint_shader;
		} else if (numeric == "SINT") {
			fragment_module = m_clear_sint_shader;
		} else {
			fragment_module = m_clear_float_shader;
		}
	}

	std::array<vk::PipelineShaderStageCreateInfo, 2> stages {};
	stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
	stages[0].module = m_vertex_shader;
	stages[0].pName  = "main";
	stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
	stages[1].module = fragment_module;
	stages[1].pName  = "main";

	vk::PipelineVertexInputStateCreateInfo vertex_input {};
	vk::PipelineInputAssemblyStateCreateInfo input_assembly {};
	input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
	vk::PipelineViewportStateCreateInfo viewport {};
	viewport.viewportCount = 1;
	viewport.scissorCount  = 1;
	vk::PipelineRasterizationStateCreateInfo rasterization {};
	rasterization.lineWidth = 1.0f;
	vk::PipelineMultisampleStateCreateInfo multisample {};
	multisample.rasterizationSamples = samples;
	vk::PipelineDepthStencilStateCreateInfo depth {};
	depth.depthTestEnable  = key.color_clear ? VK_FALSE : VK_TRUE;
	depth.depthWriteEnable = key.color_clear ? VK_FALSE : VK_TRUE;
	depth.depthCompareOp   = vk::CompareOp::eAlways;
	vk::PipelineColorBlendAttachmentState blend_attachment {};
	blend_attachment.colorWriteMask =
	    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
	    vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	vk::PipelineColorBlendStateCreateInfo color_blend {};
	if (key.color_clear) {
		color_blend.attachmentCount = 1;
		color_blend.pAttachments    = &blend_attachment;
	}
	const std::array dynamic_states {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	vk::PipelineDynamicStateCreateInfo dynamic {};
	dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
	dynamic.pDynamicStates    = dynamic_states.data();

	vk::PipelineRenderingCreateInfo rendering {};
	if (key.color_clear) {
		rendering.colorAttachmentCount    = 1;
		rendering.pColorAttachmentFormats = &key.format;
	} else {
		rendering.depthAttachmentFormat = key.format;
	}

	vk::GraphicsPipelineCreateInfo create {};
	create.pNext               = &rendering;
	create.stageCount          = static_cast<uint32_t>(stages.size());
	create.pStages             = stages.data();
	create.pVertexInputState   = &vertex_input;
	create.pInputAssemblyState = &input_assembly;
	create.pViewportState      = &viewport;
	create.pRasterizationState = &rasterization;
	create.pMultisampleState   = &multisample;
	create.pDepthStencilState  = &depth;
	create.pColorBlendState    = &color_blend;
	create.pDynamicState       = &dynamic;
	create.layout              = m_pipeline_layout;

	vk::Pipeline pipeline = nullptr;
	RequireVulkanSuccess(
	    m_graphics.device.createGraphicsPipelines(nullptr, 1, &create, nullptr, &pipeline),
	    "create BlitHelper pipeline");
	m_pipelines.push_back({key, pipeline});
	return pipeline;
}

void BlitHelper::ClearColorIndirect(vk::CommandBuffer command, vk::Format format, uint32_t samples,
                                    vk::Rect2D area, uint32_t slices, uint32_t keys,
                                    std::span<const vk::ClearAttachment> clears,
                                    vk::Buffer                           predicates) {
	command.bindPipeline(vk::PipelineBindPoint::eGraphics, GetPipeline({samples, format, true}));
	const vk::Viewport viewport {0.0f,
	                             0.0f,
	                             static_cast<float>(area.extent.width),
	                             static_cast<float>(area.extent.height),
	                             0.0f,
	                             1.0f};
	command.setViewport(0, 1, &viewport);
	command.setScissor(0, 1, &area);
	for (uint32_t key = 0; key < clears.size(); key++) {
		if ((keys & (1u << key)) == 0) {
			continue;
		}
		command.pushConstants(m_pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0,
		                      sizeof(vk::ClearColorValue), &clears[key].clearValue.color);
		for (uint32_t slice = 0; slice < slices; slice++) {
			command.drawIndirect(predicates,
			                     (slice * clears.size() + key) * sizeof(vk::DrawIndirectCommand), 1,
			                     sizeof(vk::DrawIndirectCommand));
		}
	}
	m_scheduler.GetDynamicState().Invalidate();
}

void BlitHelper::ReinterpretColorAsMsDepth(Image& source, Image& destination) {
	const auto& source_info      = source.info;
	const auto& destination_info = destination.info;
	EXIT_IF(DepthAspectTransferFormat(source_info.pixel_format) != vk::Format::eUndefined ||
	        DepthAspectTransferFormat(destination_info.pixel_format) == vk::Format::eUndefined ||
	        source_info.samples != 1 || destination_info.samples <= 1 ||
	        destination_info.samples > 4 || source.backing.image_type != vk::ImageType::e2D ||
	        destination.backing.image_type != vk::ImageType::e2D ||
	        source_info.extent.width != destination_info.extent.width ||
	        source_info.extent.height != destination_info.extent.height ||
	        source_info.extent.depth != 1 || destination_info.extent.depth != 1 ||
	        source.backing.image == nullptr || destination.backing.image == nullptr);
	m_scheduler.EndRendering();

	ImageViewInfo source_view_info {};
	source_view_info.format = source_info.pixel_format;
	source_view_info.type   = vk::ImageViewType::e2D;
	source_view_info.aspect = vk::ImageAspectFlagBits::eColor;
	source_view_info.usage  = vk::ImageUsageFlagBits::eSampled;
	const auto source_view  = source.FindView(source_view_info);

	ImageViewInfo destination_view_info {};
	destination_view_info.format = destination_info.pixel_format;
	destination_view_info.type   = vk::ImageViewType::e2D;
	destination_view_info.aspect = vk::ImageAspectFlagBits::eDepth;
	destination_view_info.usage  = vk::ImageUsageFlagBits::eDepthStencilAttachment;
	const auto destination_view  = destination.FindView(destination_view_info);

	auto& command_buffer = m_scheduler.Current();
	auto  command        = command_buffer.Handle();
	source.Transit(vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eShaderRead, {},
	               command);
	destination.Transit(ColorToMsDepthLayout, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, {},
	                    command);

	vk::RenderingAttachmentInfo depth_attachment {};
	depth_attachment.imageView               = destination_view;
	depth_attachment.imageLayout             = ColorToMsDepthLayout;
	depth_attachment.loadOp                  = vk::AttachmentLoadOp::eClear;
	depth_attachment.storeOp                 = vk::AttachmentStoreOp::eStore;
	depth_attachment.clearValue.depthStencil = {0.0f, 0};

	vk::RenderingInfo rendering {};
	rendering.renderArea.extent = {destination_info.extent.width, destination_info.extent.height};
	rendering.layerCount        = 1;
	rendering.pDepthAttachment  = &depth_attachment;
	command.beginRendering(&rendering);

	vk::DescriptorImageInfo descriptor_image {};
	descriptor_image.imageView   = source_view;
	descriptor_image.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	vk::WriteDescriptorSet descriptor_write {};
	descriptor_write.dstBinding      = 0;
	descriptor_write.descriptorCount = 1;
	descriptor_write.descriptorType  = vk::DescriptorType::eSampledImage;
	descriptor_write.pImageInfo      = &descriptor_image;
	command.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, m_pipeline_layout, 0, 1,
	                             &descriptor_write);
	command.bindPipeline(vk::PipelineBindPoint::eGraphics,
	                     GetPipeline({destination_info.samples, destination_info.pixel_format}));

	const vk::Viewport viewport {0.0f,
	                             0.0f,
	                             static_cast<float>(destination_info.extent.width),
	                             static_cast<float>(destination_info.extent.height),
	                             0.0f,
	                             1.0f};
	const vk::Rect2D   scissor {{0, 0},
	                            {destination_info.extent.width, destination_info.extent.height}};
	command.setViewport(0, 1, &viewport);
	command.setScissor(0, 1, &scissor);
	command.draw(3, 1, 0, 0);
	command.endRendering();
	// The helper pipeline has static state that invalidates the guest draw's dynamic state.
	m_scheduler.GetDynamicState().Invalidate();
}

} // namespace Libs::Graphics
