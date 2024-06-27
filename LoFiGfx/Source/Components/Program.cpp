#define NOMINMAX
#include "Program.h"

#include "../Context.h"
#include "../Message.h"

#include "../Third/spirv-cross/spirv_cross.hpp"
#include "../Third/glslang/Public/resource_limits_c.h"

using namespace LoFi::Component;

static const char* ShaderTypeHelperGetName(glslang_stage_t type) {
      switch (type) {
            case GLSLANG_STAGE_VERTEX: return "Vertex_shader";
            case GLSLANG_STAGE_TESSCONTROL: return "Tesscontrol_shader";
            case GLSLANG_STAGE_TESSEVALUATION: return "Tessevaluation_shader";
            case GLSLANG_STAGE_GEOMETRY: return "Geometry_shader";
            case GLSLANG_STAGE_FRAGMENT: return "Fragment_shader";
            case GLSLANG_STAGE_COMPUTE: return "Compute_shader";
            case GLSLANG_STAGE_RAYGEN: return "Raygen_shader";
            case GLSLANG_STAGE_INTERSECT: return "Intersect_shader";
            case GLSLANG_STAGE_ANYHIT: return "Anyhit_shader";
            case GLSLANG_STAGE_CLOSESTHIT: return "Closesthit_shader";
            case GLSLANG_STAGE_MISS: return "Miss_shader";
            case GLSLANG_STAGE_CALLABLE: return "Callable_shader";
            case GLSLANG_STAGE_TASK: return "Task_shader";
            case GLSLANG_STAGE_MESH: return "Mesh_shader";
            default: return "err shader";
      }
}

ProgramCompilerGroup::ProgramCompilerGroup() {
      glslang_initialize_process();
}

ProgramCompilerGroup::~ProgramCompilerGroup() {
      glslang_finalize_process();
}

Program::~Program() {
      for (const auto& [key, value] : _shaderModules) {
            if (value.second != VK_NULL_HANDLE) {
                  vkDestroyShaderModule(volkGetLoadedDevice(), value.second, nullptr);
            }
      }
      _shaderModules.clear();
}

Program::Program(entt::entity id) : _id(id) {
      ProgramCompilerGroup::TryInit();
}

bool Program::CompileFromSourceCode(std::string_view name, const std::vector<std::string_view>& sources) {
      _isCompiled = false;
      std::string backup_name = _programName;
      _programName = name;
      _shaderModules.clear();

      //Init Pipeline CIs, use default profile
      _inputAssemblyStateCreateInfo = VkPipelineInputAssemblyStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
      };

      _rasterizationStateCreateInfo = VkPipelineRasterizationStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f
      };

      _vertexInputStateCreateInfo = VkPipelineVertexInputStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = nullptr,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = nullptr
      };

      _depthStencilStateCreateInfo = VkPipelineDepthStencilStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f
      };

      _colorBlendStateCreateInfo = VkPipelineColorBlendStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 0,
            .pAttachments = nullptr,
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
      };

      _renderingCreateInfo = VkPipelineRenderingCreateInfoKHR{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = 0,
            .pColorAttachmentFormats = nullptr,
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
      };

      _pushConstantRange = VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_ALL,
            .offset = 0,
            .size = 0
      };

      VkShaderModuleCreateInfo shader_ci{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = 0,
            .pCode = nullptr
      };

      _vertexInputAttributeDescription.clear();
      _vertexInputBindingDescription.clear();
      _colorBlendAttachmentState.clear();
      _renderTargetFormat.clear();


      entt::dense_map<std::string, std::vector<std::string>> setters{}; // TODO: setter

      for (auto source : sources) {
            setters.clear();

            std::string setter_parse_err_msg{};
            std::string source_code_output{};

            std::optional<glslang_stage_t> find_shader_type = std::nullopt;
            std::string entry_point;
            for (auto& i : ShaderTypeMap) {
                  if (source.find(i.first) != std::string::npos) {
                        find_shader_type = i.second;
                        entry_point = i.first;
                        break;
                  }
            }

            glslang_stage_t shader_type;

            if (!find_shader_type.has_value()) {
                  const auto err = std::format("Program::CompileFromSourceCode - Failed to find shader type for shader program \"{}\", unknown shader type.",
                  _programName);
                  MessageManager::Log(MessageType::Warning, err);
                  return false;
            }

            shader_type = find_shader_type.value();
            std::string shader_type_str = ShaderTypeHelperGetName(shader_type);
            std::vector<uint32_t> spv{};
            VkShaderModule shader_module{};

            if (!ParseSetters(source, setters, source_code_output, setter_parse_err_msg, shader_type)) {
                  auto err = std::format("Program::CompileFromSourceCode - Failed to parse setters for shader program \"{}\" shader type :\"{}\".\nSetterCompiler:\n{}",
                  _programName, shader_type_str, setter_parse_err_msg);
                  MessageManager::Log(MessageType::Warning, err);
                  return false;
            }

            if (!CompileFromCode(source_code_output.data(), shader_type, spv, setter_parse_err_msg)) {
                  const auto err = std::format("Program::CompileFromSourceCode - Failed to compile shader program \"{}\", shader type :\"{}\".\nShaderCompiler:\n{}",
                  _programName, shader_type_str, setter_parse_err_msg);
                  MessageManager::Log(MessageType::Warning, err);
                  return false;
            }

            bool parse_result = false;

            switch (shader_type) {
                  case GLSLANG_STAGE_VERTEX: parse_result = ParseVS(spv);
                        break;
                  case GLSLANG_STAGE_FRAGMENT: parse_result = ParseFS(spv);
                        break;
                  default: // TODO
                        break;
            }

            if (!parse_result) {
                  const auto err = std::format("Program::CompileFromSourceCode - Failed to parse shader program \"{}\", shader type :\"{}\".",
                  _programName, shader_type_str);
                  MessageManager::Log(MessageType::Warning, err);
                  return false;
            }

            shader_ci.codeSize = spv.size() * sizeof(uint32_t);
            shader_ci.pCode = spv.data();

            if (auto res = vkCreateShaderModule(volkGetLoadedDevice(), &shader_ci, nullptr, &shader_module); res != VK_SUCCESS) {
                  const auto err = std::format("Program::CompileFromSourceCode - Failed to create shader module for shader program \"{}\", shader type :\"{}\".",
                  _programName, shader_type_str);
                  MessageManager::Log(MessageType::Warning, err);
                  return false;
            }

            _shaderModules[shader_type] = std::make_pair(std::move(spv), shader_module);
            const auto success = std::format("Program::CompileFromSourceCode - Successfully compiled shader program \"{}\", shader type :\"{}\".",
            _programName, shader_type_str);
            MessageManager::Log(MessageType::Normal, success);
      }

      _isCompiled = true;
      return true;
}

bool Program::CompileFromCode(const char* source, glslang_stage_t shader_type, std::vector<uint32_t>& spv, std::string& err_msg) {
      const glslang_input_t input = {
            .language = GLSLANG_SOURCE_GLSL,
            .stage = shader_type,
            .client = GLSLANG_CLIENT_VULKAN,
            .client_version = GLSLANG_TARGET_VULKAN_1_3,
            .target_language = GLSLANG_TARGET_SPV,
            .target_language_version = GLSLANG_TARGET_SPV_1_6,
            .code = source,
            .default_version = 460,
            .default_profile = GLSLANG_NO_PROFILE,
            .force_default_version_and_profile = false,
            .forward_compatible = false,
            .messages = GLSLANG_MSG_DEFAULT_BIT,
            .resource = glslang_default_resource(),
      };

      glslang_shader_t* shader = glslang_shader_create(&input);
      int options = 0;
      options |= GLSLANG_SHADER_AUTO_MAP_BINDINGS;
      options |= GLSLANG_SHADER_AUTO_MAP_LOCATIONS;
      options |= GLSLANG_SHADER_VULKAN_RULES_RELAXED;
      glslang_shader_set_options(shader, options);
      if (!glslang_shader_preprocess(shader, &input)) {
            err_msg = std::format("Failed to preprocess shader: {}.", glslang_shader_get_info_log(shader));
            glslang_shader_delete(shader);
            return false;
      }

      if (!glslang_shader_parse(shader, &input)) {
            err_msg = std::format("Failed to parse shader: {}.", glslang_shader_get_info_log(shader));
            glslang_shader_delete(shader);
            return false;
      }

      glslang_program_t* program = glslang_program_create();
      glslang_program_add_shader(program, shader);

      if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
            err_msg = std::format("Failed to link shader: {}.", glslang_program_get_info_log(program));
            glslang_program_delete(program);
            glslang_shader_delete(shader);
            return false;
      }

      glslang_spv_options_t spv_options = {
            .generate_debug_info = false,
            .strip_debug_info = false,
            .disable_optimizer = false,
            .optimize_size = true,
            .disassemble = false,
            .validate = true,
            .emit_nonsemantic_shader_debug_info = false,
            .emit_nonsemantic_shader_debug_source = false,
            .compile_only = false,
      };

      //glslang_program_SPIRV_generate_with_options(program, stage, &spv_options);
      glslang_program_SPIRV_generate(program, shader_type);

      spv.resize(glslang_program_SPIRV_get_size(program));
      memcpy(spv.data(), glslang_program_SPIRV_get_ptr(program), glslang_program_SPIRV_get_size(program) * sizeof(uint32_t));
      glslang_program_delete(program);
      glslang_shader_delete(shader);

      return true;
}

bool Program::ParseSetters(std::string_view codes, entt::dense_map<std::string, std::vector<std::string>>& _setters,
std::string& output_codes, std::string& error_message, glslang_stage_t shader_type) {
      std::vector<std::string_view> lines;
      std::string_view::size_type start = 0;
      std::string_view::size_type end;

      while ((end = codes.find('\n', start)) != std::string_view::npos) {
            lines.emplace_back(codes.substr(start, end - start));
            start = end + 1;
      }

      auto EatSpace = [](const std::string_view str) -> std::pair<std::string_view, std::string_view> {
            std::string_view::size_type index = 0;
            for (; index < str.size(); index++) {
                  if (str[index] == ' ' || str[index] == '\t') { continue; } else { break; }
            }
            if (index == 0) {
                  return std::make_pair(std::string_view{}, str.substr(0, str.size()));
            } else {
                  if (index >= str.size()) {
                        return std::make_pair(str.substr(0, index), std::string_view{});
                  } else {
                        return std::make_pair(str.substr(0, index), str.substr(index, str.size() - index));
                  }
            }
      };

      auto EatWord = [](const std::string_view str) -> std::optional<std::pair<std::string_view, std::string_view>> {
            std::string_view::size_type index = 0;
            for (; index < str.size(); index++) {
                  if (str[index] == ' ' || str[index] == '\t') { break; }
            }
            if (index == 0) {
                  return std::nullopt;
            } else {
                  if (index >= str.size()) {
                        return std::make_pair(str.substr(0, index), std::string_view{});
                  } else {
                        return std::make_pair(str.substr(0, index), str.substr(index, str.size() - index));
                  }
            }
      };

      std::string entry_point_str;
      for (auto& i : ShaderTypeMap) {
            if (i.second == shader_type) {
                  entry_point_str = i.first;
                  break;
            }
      }


      for (int line = 0; line < lines.size(); ++line) {
            auto piece = lines[line];

            auto pos = piece.find("#set");
            if (pos == std::string_view::npos) {
                  if (auto epos = piece.find(entry_point_str); epos != std::string_view::npos) {
                        //replace entry_point_str to main
                        auto endpos = epos + entry_point_str.size();
                        std::string newLine = std::format("{}{}{}", piece.substr(0, epos), "main", piece.substr(endpos, piece.size() - endpos));
                        output_codes += newLine;
                        output_codes += "\n";
                  } else {
                        output_codes += piece;
                        output_codes += "\n";
                  }
                  continue;
            } else {
                  auto posCommit = piece.find("//#set");
                  if (posCommit != std::string_view::npos) {
                        output_codes += piece;
                        output_codes += "\n";
                        continue;
                  } else {
                        output_codes += "//";
                        output_codes += piece;
                        output_codes += "\n";
                  }
            }

            piece = EatSpace(piece).second;

            if (auto res = EatWord(piece); res.has_value()) {
                  if (res->first == "#set") { piece = res->second; } else {
                        error_message = std::format("Program::ParseSetters - at line {} : Invalid #set statement.",
                        line);
                        return false;
                  }
            } else {
                  error_message = std::format("Program::ParseSetters - at line {} : Invalid #set statement.", line);
                  return false;
            }

            piece = EatSpace(piece).second;

            std::pair<std::string, std::vector<std::string>> setter{};

            if (auto res = EatWord(piece); res.has_value()) {
                  if (!res->first.empty()) {
                        setter.first = res->first;
                        piece = res->second;
                  } else {
                        error_message = std::format(
                        "Program::ParseSetters - at line {} : Invalid #set statement, key is empty.", line);
                        return false;
                  }
            } else {
                  error_message = std::format(
                  "Program::ParseSetters - at line {} : Invalid #set statement, key is empty.", line);
                  return false;
            }

            piece = EatSpace(piece).second;

            if (auto res = EatWord(piece); res.has_value()) {
                  if (res->first == "=") { piece = res->second; } else {
                        error_message = std::format(
                        "Program::ParseSetters - at line {} : Invalid #set statement, missing value after key \"{}\".",
                        line, setter.first);
                        return false;
                  }
            } else {
                  error_message = std::format(
                  "Program::ParseSetters - at line {} : Invalid #set statement, missing value after key \"{}\".",
                  line, setter.first);
                  return false;
            }

            piece = EatSpace(piece).second;

            if (auto res = EatWord(piece); res.has_value()) {
                  if (!res->first.empty()) {
                        setter.second.emplace_back(res->first.begin(), res->first.end());
                        piece = res->second;
                  } else {
                        error_message = std::format(
                        "Program::ParseSetters - at line {} : Invalid #set statement, value is empty.", line);
                        return false;
                  }
            } else {
                  error_message = std::format(
                  "Program::ParseSetters - at line {} : Invalid #set statement, value is empty.", line);
                  return false;
            }

            while (true) {
                  auto v = EatSpace(piece);
                  if (v.first.empty()) {
                        break;
                  } else {
                        piece = v.second;
                        if (auto res = EatWord(piece); res.has_value()) {
                              if (!res->first.empty()) {
                                    if (res->first == "//") break;
                                    setter.second.emplace_back(res->first.begin(), res->first.end());
                                    piece = res->second;
                              }
                        }
                  }
            }

            std::string analyze_error_msg{};
            if (!AnalyzeSetter(setter, analyze_error_msg, shader_type)) {
                  error_message = analyze_error_msg;
                  return false;
            }

            auto outputS_str = std::format("set {} = ", setter.first);
            for (const auto& v : setter.second) {
                  outputS_str += v;
                  outputS_str += " ";
            }
      }
      return true;
}

bool Program::ParseVS(const std::vector<uint32_t>& spv) {
      MessageManager::Log(MessageType::Normal, "Program::ParseVS - Parsing Vertex Shader");
      spirv_cross::Compiler comp(spv);
      spirv_cross::ShaderResources resources = comp.get_shader_resources();

      //input stage
      for (auto& resource : resources.stage_inputs) {
            const auto& name = comp.get_name(resource.id);
            const auto& type_id = comp.get_name(resource.base_type_id);
            auto str1 = std::format("\tInput: name {}, type {}.", name, type_id);
            std::printf("%s\n", str1.c_str());
      }

      for (auto& resource : resources.storage_buffers) {
            auto& type = comp.get_type(resource.base_type_id);

            uint32_t set = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = comp.get_decoration(resource.id, spv::Decoration::DecorationBinding);
            std::string resoure_name = comp.get_name(resource.id);
            std::string type_name = comp.get_name(resource.base_type_id);

            auto str1 = std::format("StorageBuffer : Set: {}, Binding {}, resourece name {}, warpper type name: {}.",
            set, binding, resoure_name, type_name);
            std::printf("%s\n", str1.c_str());

            uint32_t contained_type_id = type.member_types[0];
            auto contained_type = comp.get_type(contained_type_id);
            std::string contained_type_name = comp.get_name(contained_type.self);

            uint32_t member_count = contained_type.member_types.size();

            auto strSubType = std::format("\t\tWrapperType: name {}, member count {}.", contained_type_name,
            member_count);
            std::printf("%s\n", strSubType.c_str());

            for (uint32_t i = 0; i < member_count; i++) {
                  auto& member_type = comp.get_type(contained_type.member_types[i]);
                  const std::string& member_type_name = comp.get_name(member_type.self);

                  size_t member_size = comp.get_declared_struct_member_size(contained_type, i);
                  size_t offset = comp.type_struct_member_offset(contained_type, i);

                  const std::string& member_name = comp.get_member_name(contained_type.self, i);
                  auto str = std::format("\t\t{}, offset {}, size {}", member_name, offset, member_size);
                  std::printf("%s\n", str.c_str());
            }
      }

      for (auto& resource : resources.sampled_images) {
            auto& type = comp.get_type(resource.base_type_id);
            uint32_t member_count = type.member_types.size();

            uint32_t set = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = comp.get_decoration(resource.id, spv::Decoration::DecorationBinding);

            const std::string& res_name = comp.get_name(resource.id);
            auto str1 = std::format("SampledTexture: Set: {}, Binding {}, name {}.", set, binding, res_name);
            std::printf("%s\n", str1.c_str());
      }

      for (auto& resource : resources.push_constant_buffers) {
            auto& type = comp.get_type(resource.base_type_id);
            uint32_t member_count = type.member_types.size();
            const std::string& res_name = comp.get_name(resource.id);

            for (int i = 0; i < member_count; i++) {
                  auto& member_type = comp.get_type(type.member_types[i]);
                  const std::string& member_type_name = comp.get_name(member_type.self);

                  size_t member_size = comp.get_declared_struct_member_size(type, i);
                  size_t offset = comp.type_struct_member_offset(type, i);

                  const std::string& member_name = comp.get_member_name(type.self, i);
                  auto str = std::format("PushConstants: {}, offset {}, size {}", member_name, offset, member_size);
                  std::printf("%s\n", str.c_str());

                  if (i == member_count - 1) {
                        _pushConstantRange.offset = 0;
                        _pushConstantRange.size = offset + member_size;
                  }
            }
      }

      return true;
}

bool Program::ParseFS(const std::vector<uint32_t>& spv) {
      MessageManager::Log(MessageType::Normal, "Program::ParsePS - Parsing Pixel Shader");
      spirv_cross::Compiler comp(spv);
      spirv_cross::ShaderResources resources = comp.get_shader_resources();

      auto output_target_count = resources.stage_outputs.size();

      for (auto& resource : resources.stage_outputs) {
            const auto& name = comp.get_name(resource.id);
            const auto& type_id = comp.get_name(resource.base_type_id);

            auto str1 = std::format("\tOutPutTarget: name {}, type {}.", name, type_id);
            std::printf("%s\n", str1.c_str());
      }

      if (_colorBlendAttachmentState.size() != output_target_count) {
            auto str = std::format("Program::ParsePS - Output target count mismatch, expected {}, got {}, please add or move #set color_blend in shader source code.", output_target_count,
            _colorBlendAttachmentState.size());
            MessageManager::Log(MessageType::Warning, str);
            return false;
      }

      for (auto& resource : resources.storage_buffers) {
            auto& type = comp.get_type(resource.base_type_id);

            uint32_t set = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = comp.get_decoration(resource.id, spv::Decoration::DecorationBinding);
            std::string resoure_name = comp.get_name(resource.id);
            std::string type_name = comp.get_name(resource.base_type_id);

            auto str1 = std::format("StorageBuffer: Set: {}, Binding {}, resourece name {}, warpper type name: {}.",
            set, binding, resoure_name, type_name);
            std::printf("%s\n", str1.c_str());

            uint32_t contained_type_id = type.member_types[0];
            auto contained_type = comp.get_type(contained_type_id);
            std::string contained_type_name = comp.get_name(contained_type.self);

            uint32_t member_count = contained_type.member_types.size();

            auto strSubType = std::format("\t\tWrapperType: name {}, member count {}.", contained_type_name,
            member_count);
            std::printf("%s\n", strSubType.c_str());

            for (uint32_t i = 0; i < member_count; i++) {
                  auto& member_type = comp.get_type(contained_type.member_types[i]);
                  const std::string& member_type_name = comp.get_name(member_type.self);

                  size_t member_size = comp.get_declared_struct_member_size(contained_type, i);
                  size_t offset = comp.type_struct_member_offset(contained_type, i);

                  const std::string& member_name = comp.get_member_name(contained_type.self, i);
                  auto str = std::format("\t\t{}, offset {}, size {}", member_name, offset, member_size);
                  std::printf("%s\n", str.c_str());
            }
      }

      for (auto& resource : resources.sampled_images) {
            auto& type = comp.get_type(resource.base_type_id);
            uint32_t member_count = type.member_types.size();

            uint32_t set = comp.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = comp.get_decoration(resource.id, spv::Decoration::DecorationBinding);

            std::string res_name = comp.get_name(resource.id);
            auto str1 = std::format("SampledTexture: Set: {}, Binding {}, name {}.", set, binding, res_name);
            std::printf("%s\n", str1.c_str());
      }

      return true;
}

bool Program::AnalyzeSetter(const std::pair<std::string, std::vector<std::string>>& setter, std::string& error_msg, glslang_stage_t shader_type) {
      static std::unordered_map<std::string, std::unordered_map<std::string, uint64_t>> SetterKeyValueMapper{
            {
                  "topology", {
                        {"point_list", VK_PRIMITIVE_TOPOLOGY_POINT_LIST},
                        {"line_list", VK_PRIMITIVE_TOPOLOGY_LINE_LIST},
                        {"line_strip", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP},
                        {"triangle_list", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
                        {"triangle_strip", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP},
                        {"triangle_fan", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN},
                        {"line_list_with_adjacency", VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY},
                        {"line_strip_with_adjacency", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY},
                        {"triangle_list_with_adjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY},
                        {"triangle_strip_with_adjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY},
                        {"patch_list", VK_PRIMITIVE_TOPOLOGY_PATCH_LIST},
                  }
            },
            {
                  "polygon_mode", {
                        {"fill", VK_POLYGON_MODE_FILL},
                        {"line", VK_POLYGON_MODE_LINE},
                        {"point", VK_POLYGON_MODE_POINT},
                  }
            },
            {
                  "cull_mode", {
                        {"front", VK_CULL_MODE_FRONT_BIT},
                        {"back", VK_CULL_MODE_BACK_BIT},
                        {"none", VK_CULL_MODE_NONE},
                        {"front_and_back", VK_CULL_MODE_FRONT_AND_BACK},
                  }
            },
            {
                  "front_face", {
                        {"clockwise", VK_FRONT_FACE_CLOCKWISE},
                        {"counter_clockwise", VK_FRONT_FACE_COUNTER_CLOCKWISE},
                  }
            },
            {
                  "depth_write", {
                        {"true", VK_TRUE},
                        {"false", VK_FALSE},
                  }
            },
            {
                  "depth_test", {
                        {"default", VK_COMPARE_OP_LESS_OR_EQUAL},
                        {"never", VK_COMPARE_OP_NEVER},
                        {"less", VK_COMPARE_OP_LESS},
                        {"equal", VK_COMPARE_OP_EQUAL},
                        {"less_or_equal", VK_COMPARE_OP_LESS_OR_EQUAL},
                        {"greater", VK_COMPARE_OP_GREATER},
                        {"not_equal", VK_COMPARE_OP_NOT_EQUAL},
                        {"greater_or_equal", VK_COMPARE_OP_GREATER_OR_EQUAL},
                        {"always", VK_COMPARE_OP_ALWAYS},
                  }
            }
      };

      static std::unordered_map<glslang_stage_t, std::set<std::string>> SetterAvailableInShaderType{
            {
                  glslang_stage_t::GLSLANG_STAGE_VERTEX, {
                        "vs_location",
                        "vs_binding",
                        "topology",
                        "polygon_mode",
                        "cull_mode",
                        "front_face",

                        "depth_write",
                        "depth_test",
                        "depth_bias",
                        "depth_bounds_test",
                        "line_width"
                  }
            },

            {
                  glslang_stage_t::GLSLANG_STAGE_FRAGMENT, {
                        "rt",
                        "ds",
                        "color_blend",

                        "depth_write",
                        "depth_test",
                        "depth_bias",
                        "depth_bounds_test",
                        "line_width"
                  }
            }

      };

      auto CheckAndFetchValue = [&](const std::string& the_key, const std::string& the_value,
      std::string& error) -> std::optional<uint64_t> {
            if (const auto it = SetterKeyValueMapper.find(the_key); it != SetterKeyValueMapper.end()) {
                  if (const auto it2 = it->second.find(the_value); it2 != it->second.end()) {
                        return it2->second;
                  } else {
                        error = std::format(R"(Invalid value "{}" for key "{}", )", the_value, the_key);
                        error += std::format("Valid values are : \n\t\t");
                        for (const auto& [k, v] : it->second) {
                              error += k;
                              error += " ";
                        }

                        return std::nullopt;
                  }
            } else {
                  error = std::format("Invalid setter key \"{}\" .", the_key);
                  return std::nullopt;
            }
      };

      auto Analyze = [&]<typename T0>(T0& input, const std::string& key, const std::string& the_value,
      std::string& error) -> bool {
            if (const auto res = CheckAndFetchValue(key, the_value, error_msg); res.has_value()) {
                  input = *((T0*)&res.value());
                  return true;
            }
            return false;
      };

      auto ErrorArgumentUnmatching = [](const std::string& key, uint32_t argument_target, uint32_t error_argument, std::string& error_msg, const std::string& argument_format) -> bool {
            error_msg = std::format("Invalid argument count for key \"{}\". Expected {}, got {}, format: [{}].",
            key, argument_target, error_argument, argument_format);
            return false;
      };

      auto ErrorArgument = [](const std::string& key, uint32_t err_target, const std::string& value, std::string& error_msg, const std::string& vaild_values) -> bool {
            error_msg = std::format("Invalid argument {} for key \"{}\". Expected [{}], got \"{}\".", key, err_target, vaild_values, value);
            return false;
      };

      auto ErrorSetInShaderType = [](const std::string& key, glslang_stage_t shader_type, std::string& error_msg) -> bool {
            error_msg = std::format("Invalid setter key \"{}\" for shader type \"{}\".", key, ShaderTypeHelperGetName(shader_type));
            return false;
      };

      const auto& key = setter.first;
      const auto& values = setter.second;

      auto vaild_setters = SetterAvailableInShaderType.find(shader_type);
      if (vaild_setters == SetterAvailableInShaderType.end()) {
            //shouldn't be here
            return ErrorSetInShaderType(key, shader_type, error_msg);
      }

      if (!vaild_setters->second.contains(key)) {
            error_msg = std::format("Invalid setter key \"{}\" for shader type \"{}\".", key, ShaderTypeHelperGetName(shader_type));
            return false;
      }


      if (key == "topology") {
            if (!Analyze(_inputAssemblyStateCreateInfo.topology, key, values[0], error_msg)) return false;
      } else if (key == "polygon_mode") {
            if (!Analyze(_rasterizationStateCreateInfo.polygonMode, key, values[0], error_msg)) return false;
      } else if (key == "cull_mode") {
            if (!Analyze(_rasterizationStateCreateInfo.cullMode, key, values[0], error_msg)) return false;
      } else if (key == "front_face") {
            if (!Analyze(_rasterizationStateCreateInfo.frontFace, key, values[0], error_msg)) return false;
      } else if (key == "depth_write") {
            if (!Analyze(_depthStencilStateCreateInfo.depthWriteEnable, key, values[0], error_msg)) return false;
      } else if (key == "depth_test") {
            _depthStencilStateCreateInfo.depthTestEnable = true;
            if (!Analyze(_depthStencilStateCreateInfo.depthCompareOp, key, values[0], error_msg)) return false;
      } else if (key == "vs_location") {
            if (values.size() == 4) {
                  // bind, location, format, offset
                  uint32_t binding;
                  uint32_t location;
                  uint32_t offset;
                  try {
                        binding = std::stoi(values[0]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 1 for key \"{}\". Expected integer, got \"{}\".", key, values[0]);
                        return false;
                  }

                  try {
                        location = std::stoi(values[1]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 2 for key \"{}\". Expected integer, got \"{}\".", key, values[1]);
                        return false;
                  }

                  try {
                        offset = std::stoi(values[3]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 4 for key \"{}\". Expected integer, got \"{}\".", key, values[3]);
                        return false;
                  }

                  VkFormat format = GetVkFormatFromStringSimpled(values[2]);

                  if (format == VK_FORMAT_UNDEFINED) {
                        return ErrorArgument(key, 3, values[2], error_msg, "(format)");
                  }

                  VkVertexInputAttributeDescription att_desc{
                        .location = location,
                        .binding = binding,
                        .format = format,
                        .offset = offset
                  };

                  _vertexInputAttributeDescription.push_back(att_desc);

                  _vertexInputStateCreateInfo.pVertexAttributeDescriptions = _vertexInputAttributeDescription.data();
                  _vertexInputStateCreateInfo.vertexAttributeDescriptionCount = _vertexInputAttributeDescription.size();
            } else {
                  return ErrorArgumentUnmatching(key, 4, values.size(), error_msg, "bind, location , format, offset");
            }
      } else if (key == "vs_binding") {
            if (values.size() == 3) {
                  // binding, stride_size, binding_rate
                  uint32_t binding;
                  uint32_t stride_size;

                  try {
                        binding = std::stoi(values[0]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 1 for key \"{}\". Expected integer, got \"{}\".", key, values[0]);
                        return false;
                  }

                  try {
                        stride_size = std::stoi(values[1]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 2 for key \"{}\". Expected integer, got \"{}\".", key, values[1]);
                        return false;
                  }

                  VkVertexInputBindingDescription binding_desc{
                        .binding = binding,
                        .stride = stride_size,
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                  };

                  if (values[2] == "vertex") {
                        binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                  } else if (values[2] == "instance") {
                        binding_desc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
                  } else {
                        return ErrorArgument(key, 3, values[2], error_msg, "vertex, instance");
                  }
                  _vertexInputBindingDescription.push_back(binding_desc);
                  _vertexInputStateCreateInfo.pVertexBindingDescriptions = _vertexInputBindingDescription.data();
                  _vertexInputStateCreateInfo.vertexBindingDescriptionCount = _vertexInputBindingDescription.size();
            } else {
                  return ErrorArgumentUnmatching(key, 4, values.size(), error_msg, "binding, stride_size, binding_rate");
            }
      } else if (key == "depth_bias") {
            if (values.size() == 3) {
                  // depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor
                  float depthBiasConstantFactor;
                  float depthBiasClamp;
                  float depthBiasSlopeFactor;

                  try {
                        depthBiasConstantFactor = std::stof(values[0]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 1 for key \"{}\". Expected float, got \"{}\".", key, values[0]);
                        return false;
                  }

                  try {
                        depthBiasClamp = std::stof(values[1]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 2 for key \"{}\". Expected float, got \"{}\".", key, values[1]);
                        return false;
                  }

                  try {
                        depthBiasSlopeFactor = std::stof(values[2]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 3 for key \"{}\". Expected float, got \"{}\".", key, values[2]);
                        return false;
                  }

                  _rasterizationStateCreateInfo.depthBiasEnable = true;
                  _rasterizationStateCreateInfo.depthBiasConstantFactor = depthBiasConstantFactor;
                  _rasterizationStateCreateInfo.depthBiasClamp = depthBiasClamp;
                  _rasterizationStateCreateInfo.depthBiasSlopeFactor = depthBiasSlopeFactor;
            } else {
                  return ErrorArgumentUnmatching(key, 3, values.size(), error_msg, "depth_bias_constant_factor, depth_bias_clamp, depth_bias_slope_factor");
            }
      } else if (key == "depth_bounds_test") {
            if (values.size() == 2) {
                  // min_depth, max_depth
                  float min_depth;
                  float max_depth;

                  try {
                        min_depth = std::stof(values[0]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 1 for key \"{}\". Expected float, got \"{}\".", key, values[0]);
                        return false;
                  }

                  try {
                        max_depth = std::stof(values[1]);
                  } catch (std::runtime_error& what) {
                        error_msg = std::format("Invalid argument 2 for key \"{}\". Expected float, got \"{}\".", key, values[1]);
                        return false;
                  }

                  _depthStencilStateCreateInfo.depthBoundsTestEnable = true;
                  _depthStencilStateCreateInfo.minDepthBounds = min_depth;
                  _depthStencilStateCreateInfo.maxDepthBounds = max_depth;
            } else {
                  return ErrorArgumentUnmatching(key, 2, values.size(), error_msg, "min_depth, max_depth");
            }
      } else if (key == "line_width") {
            float line_width;

            try {
                  line_width = std::stof(values[0]);
            } catch (std::runtime_error& what) {
                  error_msg = std::format("Invalid argument 1 for key \"{}\". Expected float, got \"{}\".", key, values[0]);
                  return false;
            }
            _rasterizationStateCreateInfo.lineWidth = line_width;
      } else if (key == "color_blend") {
            if (values.size() == 1) {
                  // blend_enable
                  if (values[0] == "false") {
                        VkPipelineColorBlendAttachmentState default_profile{};
                        default_profile.blendEnable = VK_FALSE;
                        default_profile.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        default_profile.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        default_profile.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                        default_profile.colorBlendOp = VK_BLEND_OP_ADD;
                        default_profile.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        default_profile.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                        default_profile.alphaBlendOp = VK_BLEND_OP_ADD;
                        _colorBlendAttachmentState.push_back(default_profile);
                  } else if (values[0] == "add") {
                        VkPipelineColorBlendAttachmentState additive_blend = {};
                        additive_blend.blendEnable = VK_TRUE;
                        additive_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        additive_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                        additive_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        additive_blend.colorBlendOp = VK_BLEND_OP_ADD;
                        additive_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        additive_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                        additive_blend.alphaBlendOp = VK_BLEND_OP_ADD;
                        _colorBlendAttachmentState.push_back(additive_blend);
                  } else if (values[0] == "multiply") {
                        VkPipelineColorBlendAttachmentState multiplicative_blend = {};
                        multiplicative_blend.blendEnable = VK_TRUE;
                        multiplicative_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        multiplicative_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        multiplicative_blend.colorBlendOp = VK_BLEND_OP_MULTIPLY_EXT;
                        multiplicative_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        multiplicative_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        multiplicative_blend.alphaBlendOp = VK_BLEND_OP_MULTIPLY_EXT;
                        multiplicative_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        _colorBlendAttachmentState.push_back(multiplicative_blend);
                  } else if (values[0] == "sub") {
                        VkPipelineColorBlendAttachmentState subtractive_blend = {};
                        subtractive_blend.blendEnable = VK_TRUE;
                        subtractive_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                        subtractive_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        subtractive_blend.colorBlendOp = VK_BLEND_OP_SUBTRACT;
                        subtractive_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                        subtractive_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        subtractive_blend.alphaBlendOp = VK_BLEND_OP_SUBTRACT;
                        subtractive_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        _colorBlendAttachmentState.push_back(subtractive_blend);
                  } else if (values[0] == "alpha") {
                        VkPipelineColorBlendAttachmentState alpha_blend = {};
                        alpha_blend.blendEnable = VK_TRUE;
                        alpha_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        alpha_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                        alpha_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                        alpha_blend.colorBlendOp = VK_BLEND_OP_ADD;
                        alpha_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        alpha_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                        alpha_blend.alphaBlendOp = VK_BLEND_OP_ADD;
                        _colorBlendAttachmentState.push_back(alpha_blend);
                  } else if (values[0] == "screen") {
                        VkPipelineColorBlendAttachmentState screen_blend = {};
                        screen_blend.blendEnable = VK_TRUE;
                        screen_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        screen_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        screen_blend.colorBlendOp = VK_BLEND_OP_SCREEN_EXT;
                        screen_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        screen_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        screen_blend.alphaBlendOp = VK_BLEND_OP_SCREEN_EXT;
                        screen_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        _colorBlendAttachmentState.push_back(screen_blend);
                  } else if (values[0] == "darken") {
                        VkPipelineColorBlendAttachmentState darken_blend = {};
                        darken_blend.blendEnable = VK_TRUE;
                        darken_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        darken_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        darken_blend.colorBlendOp = VK_BLEND_OP_MIN;
                        darken_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        darken_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        darken_blend.alphaBlendOp = VK_BLEND_OP_MIN;
                        darken_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        _colorBlendAttachmentState.push_back(darken_blend);
                  } else if (values[0] == "lighten") {
                        VkPipelineColorBlendAttachmentState lighten_blend = {};
                        lighten_blend.blendEnable = VK_TRUE;
                        lighten_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        lighten_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        lighten_blend.colorBlendOp = VK_BLEND_OP_MAX;
                        lighten_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        lighten_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        lighten_blend.alphaBlendOp = VK_BLEND_OP_MAX;
                        lighten_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        _colorBlendAttachmentState.push_back(lighten_blend);
                  } else if (values[0] == "difference") {
                        VkPipelineColorBlendAttachmentState difference_blend = {};
                        difference_blend.blendEnable = VK_TRUE;
                        difference_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        difference_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        difference_blend.colorBlendOp = VK_BLEND_OP_DIFFERENCE_EXT;
                        difference_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        difference_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        difference_blend.alphaBlendOp = VK_BLEND_OP_DIFFERENCE_EXT;
                        difference_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        _colorBlendAttachmentState.push_back(difference_blend);
                  } else if (values[0] == "exclusion") {
                        VkPipelineColorBlendAttachmentState exclusion_blend = {};
                        exclusion_blend.blendEnable = VK_TRUE;
                        exclusion_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        exclusion_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                        exclusion_blend.colorBlendOp = VK_BLEND_OP_EXCLUSION_EXT;
                        exclusion_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        exclusion_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                        exclusion_blend.alphaBlendOp = VK_BLEND_OP_EXCLUSION_EXT;
                        exclusion_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                        _colorBlendAttachmentState.push_back(exclusion_blend);
                  } else {
                        return ErrorArgument(key, 1, values[0], error_msg, "(false) or (add, multiply, sub, alpha, screen, darken, lighten, difference, exclusion)");
                        //return ErrorArgumentUnmatching(key, 1, values.size(), error_msg, "if you enable color blend, please use argument: (defualt) or (colorWriteMask, dstAlphaBlendFactor, srcAlphaBlendFactor, colorBlendOp, alphaBlendOp)");
                  }
                  _colorBlendStateCreateInfo.attachmentCount = _colorBlendAttachmentState.size();
                  _colorBlendStateCreateInfo.pAttachments = _colorBlendAttachmentState.data();
            } else {
                  return ErrorArgumentUnmatching(key, 1, values.size(), error_msg, "(blend_enable) or (colorWriteMask, dstAlphaBlendFactor, srcAlphaBlendFactor, colorBlendOp, alphaBlendOp)");
            }
      } else if (key == "rt") {
            for (int idx = 0; idx < values.size(); idx++) {
                  VkFormat vk_format = GetVkFormatFromStringSimpled(values[idx]);

                  if (vk_format == VK_FORMAT_UNDEFINED) {
                        error_msg = std::format("Invalid argument {} for key \"{}\". Expected a vaild format, got \"{}\".", idx, key, values[idx]);
                        return false;
                  }

                  _renderTargetFormat.push_back(vk_format);
            }
            _renderingCreateInfo.colorAttachmentCount = _renderTargetFormat.size();
            _renderingCreateInfo.pColorAttachmentFormats = _renderTargetFormat.data();
      } else if (key == "ds") {
            if (values.size() == 1) {
                  VkFormat vk_format = GetVkFormatFromStringSimpled(values[0]);
                  if (IsDepthStencilOnlyFormat(vk_format)) {
                        _renderingCreateInfo.depthAttachmentFormat = vk_format;
                        _renderingCreateInfo.stencilAttachmentFormat = vk_format;
                  } else if (IsDepthOnlyFormat(vk_format)) {
                        _renderingCreateInfo.depthAttachmentFormat = vk_format;
                        _renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
                  } else {
                        error_msg = std::format(
                        "Invalid argument {} for key \"{}\". Expected a vaild format, got \"{}\", vaild format:[d16_unorm_s8_uint, d24_unorm_s8_uint, d32_sfloat_s8_uint, d16_unorm, d32_sfloat].",
                        0, key, values[0]);
                        return false;
                  }
            } else {
                  error_msg = std::format(
                  "Invalid argument count for key \"{}\". Expected 1 , got {}, format: (depth_(stencil)_format, vaild format:[d16_unorm_s8_uint, d24_unorm_s8_uint, d32_sfloat_s8_uint, d16_unorm, d32_sfloat]).",
                  key, values.size());
                  return false;
            }
      } else {
            error_msg = std::format("Invalid setter key \"{}\" .", key);
            return false;
      }

      return true;
}
