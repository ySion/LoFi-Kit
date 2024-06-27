//
// Created by starr on 2024/6/23.
//

#include "GraphicKernel.h"
#include "Program.h"

#include "../Message.h"
#include "../Context.h"

using namespace LoFi::Component;
using namespace LoFi::Internal;

GraphicKernel::GraphicKernel(entt::entity id) : _id(id) {
}

GraphicKernel::~GraphicKernel() {
      if (_pipeline) {
            vkDestroyPipeline(volkGetLoadedDevice(), _pipeline, nullptr);
      }

      if (_pipelineLayout) {
            vkDestroyPipelineLayout(volkGetLoadedDevice(), _pipelineLayout, nullptr);
      }
}

std::optional<BindlessLayoutVariableInfo> GraphicKernel::SetBindlessLayoutVariable(const std::string& name) const {
      if(const auto find = _pushConstantBufferVariableMap.find(name); find != _pushConstantBufferVariableMap.end())
            return find->second;
      return std::nullopt;
}

bool GraphicKernel::SetBindlessLayoutVariable(const std::string& name, const void* data) const {
      if(const auto find = _pushConstantBufferVariableMap.find(name); find != _pushConstantBufferVariableMap.end()) {
            const uint8_t* p = _pushConstantBuffer.data() + find->second.offset;
            memcpy((void*)p, data, find->second.size);
            return true;
      }
      return false;
}

void GraphicKernel::PushConstantBindlessLayoutVariableInfo(VkCommandBuffer cmd) const {
      vkCmdPushConstants(cmd, _pipelineLayout, VK_SHADER_STAGE_ALL, 0, (uint32_t)_pushConstantBuffer.size(), _pushConstantBuffer.data());
}

bool GraphicKernel::CreateFromProgram(entt::entity program) {
      auto& world = *volkGetLoadedEcsWorld();

      if (!world.valid(program)) {
            auto str = std::format("GraphicKernel::CreateFromProgram - Invalid Program Entity\n");
            MessageManager::Log(MessageType::Warning, str);
            return false;
      }

      auto prog = world.try_get<Program>(program);
      if (!prog) {
            auto str = std::format("GraphicKernel::CreateFromProgram - Program Component Not Found\n");
            MessageManager::Log(MessageType::Warning, str);
            return false;
      }

      if (!prog->IsCompiled()) {
            const auto err = std::format("GraphicKernel::CreateFromProgram - Program Not Compiled\n");
            MessageManager::Log(MessageType::Warning, err);
            return false;
      }

      if (!prog->GetShaderModules().contains(glslang_stage_t::GLSLANG_STAGE_VERTEX)) {
            auto str = std::format("GraphicKernel::CreateFromProgram - Vertex Shader Not Found\n");
            MessageManager::Log(MessageType::Warning, str);
            return false;
      }

      if (!prog->GetShaderModules().contains(glslang_stage_t::GLSLANG_STAGE_FRAGMENT)) {
            auto str = std::format("GraphicKernel::CreateFromProgram - Pixel Shader Not Found\n");
            MessageManager::Log(MessageType::Warning, str);
            return false;
      }

      std::vector<VkPipelineShaderStageCreateInfo> stages{
            {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = prog->GetShaderModules().at(glslang_stage_t::GLSLANG_STAGE_VERTEX).second,
                  .pName = "main"
            },
            {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = prog->GetShaderModules().at(glslang_stage_t::GLSLANG_STAGE_FRAGMENT).second,
                  .pName = "main"
            }
      };

      VkPipelineViewportStateCreateInfo viewport_ci{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
      };

      VkPipelineMultisampleStateCreateInfo multisample_ci{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE
      };

      std::vector<VkDynamicState> dynamic_states{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
      };

      VkPipelineDynamicStateCreateInfo dynamic_state_ci{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = (uint32_t)dynamic_states.size(),
            .pDynamicStates = dynamic_states.data()
      };

      VkPipelineLayoutCreateInfo pipeline_layout_ci{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &LoFi::Context::Get()->_bindlessDescriptorSetLayout,
            .pushConstantRangeCount = (uint32_t)(prog->_pushConstantRange.size == 0 ? 0 : 1),
            .pPushConstantRanges = (prog->_pushConstantRange.size == 0 ? nullptr : &prog->_pushConstantRange)
      };

      if (vkCreatePipelineLayout(volkGetLoadedDevice(), &pipeline_layout_ci, nullptr, &_pipelineLayout) != VK_SUCCESS) {
            auto str = std::format("GraphicKernel::CreateFromProgram - vkCreatePipelineLayout Failed\n");
            MessageManager::Log(MessageType::Error, str);
            return false;
      }

      VkGraphicsPipelineCreateInfo pipeline_ci{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &prog->_renderingCreateInfo,
            .flags = 0,
            .stageCount = (uint32_t)stages.size(),
            .pStages = stages.data(),
            .pVertexInputState = &prog->_vertexInputStateCreateInfo,
            .pInputAssemblyState = &prog->_inputAssemblyStateCreateInfo,
            .pTessellationState = nullptr,
            .pViewportState = &viewport_ci,
            .pRasterizationState = &prog->_rasterizationStateCreateInfo,
            .pMultisampleState = &multisample_ci,
            .pDepthStencilState = &prog->_depthStencilStateCreateInfo,
            .pColorBlendState = &prog->_colorBlendStateCreateInfo,
            .pDynamicState = &dynamic_state_ci,
            .layout = _pipelineLayout,
            .renderPass = nullptr,
            .subpass = 0,
            .basePipelineHandle = nullptr,
            .basePipelineIndex = 0
      };

      if (vkCreateGraphicsPipelines(volkGetLoadedDevice(), nullptr, 1, &pipeline_ci, nullptr, &_pipeline) != VK_SUCCESS) {
            auto str = std::format("GraphicKernel::CreateFromProgram - vkCreateGraphicsPipelines Failed\n");
            MessageManager::Log(MessageType::Error, str);
            return false;
      }

      _pushConstantBufferVariableMap = prog->_shaderPushConstantMap_BindlessLayoutVariableInfo;
      _pushConstantBuffer.resize(prog->_pushConstantRange.size);

      return true;
}
