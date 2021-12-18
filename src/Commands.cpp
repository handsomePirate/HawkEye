#include "Commands.hpp"
#include "Resources.hpp"
#include <VulkanBackend/ErrorCheck.hpp>

void CommandUtils::Record(int c, const VulkanBackend::BackendData& backendData, HawkEye::Pipeline::Private* pipelineData)
{
	// TODO: In the case of computed passes, we probably want to have the compute buffers be from the compute queue (it can also present).
	VkCommandBuffer commandBuffer = pipelineData->frameData[c].commandBuffer;

	VkCommandBufferBeginInfo commandBufferBeginInfo{};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	// TODO: Multi-threaded recording of command buffers.
	VulkanCheck(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

	// TODO: Make compatible with any order of rasterized and compute passes.
	for (int p = 0; p < pipelineData->passData.size(); ++p)
	{
		// TODO: Computed passes need the option to inherit depth buffers.
		if (pipelineData->passData[p].type == PipelinePass::Type::Computed)
		{
			// TODO: Materials could be useful here as well (-> multiple dispatches per pass).
			VulkanBackend::TransitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
				pipelineData->swapchainImages[c], 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				VK_IMAGE_ASPECT_COLOR_BIT, 0, 0);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineData->passData[p].computePipeline);

			std::vector<VkDescriptorSet> descriptorSets;
			descriptorSets.reserve(4);
			descriptorSets.push_back(pipelineData->passData[p].frameDescriptors[c].descriptorSet);
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
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineData->passData[p].pipelineLayout, 0,
					(uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
			}

			// TODO: Works in multiples of 16, make sure that exactly the entire picture is rendered onto the screen.
			vkCmdDispatch(commandBuffer, (pipelineData->surfaceData->width + 15) / 16, (pipelineData->surfaceData->height + 15) / 16, 1);

			// TODO: Next pass could also be rasterized if the following one is empty.
			if (p < pipelineData->passData.size() - 1 &&
				(pipelineData->passData[p + 1].type != PipelinePass::Type::Rasterized || pipelineData->passData[p + 1].empty))
			{
				VulkanBackend::TransitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					pipelineData->swapchainImages[c], 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, 0);
			}
		}
		else
		{
			if (pipelineData->passData[p].materials.size() == 0 || pipelineData->passData[p].empty)
			{
				continue;
			}

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

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData->passData[p].rasterizationPipeline);

			for (int m = 0; m < pipelineData->passData[p].materials.size(); ++m)
			{
				auto it = pipelineData->passData[p].drawBuffers.find(m);

				std::vector<VkDescriptorSet> descriptorSets;
				descriptorSets.reserve(4);
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
						(uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
				}

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

			vkCmdEndRenderPass(commandBuffer);
		}
	}

	VulkanCheck(vkEndCommandBuffer(commandBuffer));

	pipelineData->frameData[c].dirty = false;
}
