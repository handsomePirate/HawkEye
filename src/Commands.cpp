#include "Commands.hpp"
#include "Resources.hpp"
#include <VulkanBackend/ErrorCheck.hpp>

void CommandUtils::Record(int c, const VulkanBackend::BackendData& backendData, HawkEye::Pipeline::Private* pipelineData)
{
	// TODO: In the case of computed passes, we probably want to have the compute buffers be from the compute queue (it can also present).
	VkCommandBuffer commandBuffer = pipelineData->frameData[c].commandBuffer;

	VkCommandBufferBeginInfo commandBufferBeginInfo{};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	VulkanCheck(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

	// TODO: Make compatible with any order of rasterized and compute passes.
	if (pipelineData->passData[0].type == PipelinePass::Type::Computed)
	{
		VulkanBackend::TransitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			pipelineData->swapchainImages[c], 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 0);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineData->passData[0].computePipeline);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineData->passData[0].pipelineLayout, 0, 1,
			&pipelineData->frameData[c].frameDescriptors.descriptorSet, 0, 0);

		// TODO: Works in multiples of 16, make sure that exactly the entire picture is rendered onto the screen.
		vkCmdDispatch(commandBuffer, pipelineData->surfaceData->width / 16, pipelineData->surfaceData->height / 16, 1);

		VulkanBackend::TransitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			pipelineData->swapchainImages[c], 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 0);
	}
	else
	{
		const int clearValueCount = 2;
		VkClearValue clearValues[clearValueCount];
		clearValues[0].color = { 0.f, 0.f, 0.f };
		clearValues[1].depthStencil = { 1.f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = pipelineData->renderPass;
		renderPassBeginInfo.renderArea.extent.width = pipelineData->surfaceData->width;
		renderPassBeginInfo.renderArea.extent.height = pipelineData->surfaceData->height;
		renderPassBeginInfo.clearValueCount = clearValueCount - (pipelineData->hasDepthTarget ? 0 : 1);
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = pipelineData->framebuffers[c];

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// HACK: Without dynamic viewports and other states, the pipeline might need to be recreated.
		VkViewport viewport;
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = (float)pipelineData->surfaceData->width;
		viewport.height = (float)pipelineData->surfaceData->height;
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

		VkRect2D scissor;
		scissor.offset = { 0, 0 };
		scissor.extent = VkExtent2D{ (uint32_t)pipelineData->surfaceData->width, (uint32_t)pipelineData->surfaceData->height };
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		for (int p = 0; p < pipelineData->passData.size(); ++p)
		{
			if (!pipelineData->passData[p].inheritDepth && p > 0)
			{
				VkClearAttachment clearAttachment{};
				clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				VkClearValue clearValue{};
				clearValue.depthStencil = { 1.f, 0 };
				clearAttachment.clearValue = clearValue;
				VkClearRect clearRect{};
				clearRect.baseArrayLayer = 0;
				clearRect.layerCount = 1;
				VkRect2D rect{};
				rect.extent.width = pipelineData->surfaceData->width;
				rect.extent.height = pipelineData->surfaceData->height;
				clearRect.rect = rect;
				vkCmdClearAttachments(commandBuffer, 1, &clearAttachment, 1, &clearRect);
			}

			if (pipelineData->passData[p].materials.size() == 0)
			{
				continue;
			}

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData->passData[p].rasterizationPipeline);

			for (int m = 0; m < pipelineData->passData[p].materials.size(); ++m)
			{
				auto it = pipelineData->passData[p].drawBuffers.find(m);
				if (it == pipelineData->passData[p].drawBuffers.end())
				{
					continue;
				}

				std::vector<VkDescriptorSet> descriptorSets;
				descriptorSets.reserve(2);
				if (pipelineData->passData[p].materials[m].descriptorSet != VK_NULL_HANDLE)
				{
					descriptorSets.push_back(pipelineData->passData[p].materials[m].descriptorSet);
				}
				if (pipelineData->passData[p].descriptorData.descriptorSet != VK_NULL_HANDLE)
				{
					descriptorSets.push_back(pipelineData->passData[p].descriptorData.descriptorSet);
				}
				if (pipelineData->descriptorData.descriptorSet != VK_NULL_HANDLE)
				{
					descriptorSets.push_back(pipelineData->descriptorData.descriptorSet);
				}
				if (descriptorSets.size() > 0)
				{
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData->passData[p].pipelineLayout, 0,
						descriptorSets.size(), descriptorSets.data(), 0, nullptr);
				}

				// Display ray traced image generated by compute shader as a full screen quad
				// Quad vertices are generated in the vertex shader
				for (int b = 0; b < it->second.size(); ++b)
				{
					static VkDeviceSize offset = 0;
					vkCmdBindVertexBuffers(commandBuffer, 0, 1, &it->second[b].vertexBuffer->buffer.buffer, &offset);
					vkCmdBindVertexBuffers(commandBuffer, 1, 1, &it->second[b].instanceBuffer->buffer.buffer, &offset);
					if (it->second[b].indexBuffer)
					{
						vkCmdBindIndexBuffer(commandBuffer, it->second[b].indexBuffer->buffer.buffer, offset, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(commandBuffer, it->second[b].indexBuffer->dataSize / 4, it->second[b].instanceBuffer->dataSize / 64, 0, 0, 0);
					}
					else
					{
						vkCmdDraw(commandBuffer, it->second[b].vertexBuffer->dataSize / pipelineData->passData[p].vertexSize, it->second[b].instanceBuffer->dataSize / 64, 0, 0);
					}
				}
			}
		}

		vkCmdEndRenderPass(commandBuffer);
	}

	VulkanCheck(vkEndCommandBuffer(commandBuffer));

	pipelineData->frameData[c].dirty = false;
}
