// Copyright (c) 2026 Evangelion Manuhutu

#include "Umbra/ShaderCompiler.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    bool IsShaderSourceFile(const std::filesystem::path& path)
    {
        const std::string ext = ToLower(path.extension().string());
        return ext == ".hlsl" || ext == ".glsl";
    }

    bool IsHlslFile(const std::filesystem::path& path)
    {
        return ToLower(path.extension().string()) == ".hlsl";
    }

    std::string DetectOutputDirectory(const std::filesystem::path& inputPath)
    {
        const std::string normalized = umbra::PathToString(inputPath);
        if (normalized.find("/HLSL/") != std::string::npos || normalized.find("\\HLSL\\") != std::string::npos)
        {
            return "Shaders/Compiled/HSLSL";
        }

        if (normalized.find("/GLSL/") != std::string::npos || normalized.find("\\GLSL\\") != std::string::npos)
        {
            return "Shaders/Compiled/GLSL";
        }

        return "Shaders/Compiled/Misc";
    }

    UMBRA_ShaderType DetectShaderTypeFromFilename(const std::string& filename)
    {
        const std::string lower = ToLower(filename);
        if (lower.find(".vertex.") != std::string::npos) return UMBRA_SHADER_TYPE_VERTEX;
        if (lower.find(".pixel.") != std::string::npos) return UMBRA_SHADER_TYPE_PIXEL;
        if (lower.find(".geometry.") != std::string::npos) return UMBRA_SHADER_TYPE_GEOMETRY;
        if (lower.find(".compute.") != std::string::npos) return UMBRA_SHADER_TYPE_COMPUTE;
        if (lower.find(".tessellation.") != std::string::npos) return UMBRA_SHADER_TYPE_TESSELLATION;
        return UMBRA_SHADER_TYPE_VERTEX;
    }

    bool ReadBinaryFile(const std::filesystem::path& filePath, std::vector<uint8_t>& outBytes)
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return false;
        }

        const std::streamsize size = file.tellg();
        if (size <= 0)
        {
            return false;
        }

        outBytes.resize(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        return file.read(reinterpret_cast<char*>(outBytes.data()), size).good();
    }

    void PrintReflectionSummary(const char* label, const umbra::ShaderReflectionInfo& reflection)
    {
        std::cout
            << "  [" << label << " Reflection] type=" << UMBRA_GetShaderTypeString(reflection.shaderType)
            << ", UBO=" << reflection.numUniformBuffers
            << ", Samplers=" << reflection.numSamplers
            << ", StorageTex=" << reflection.numStorageTextures
            << ", StorageBuf=" << reflection.numStorageBuffers
            << ", Inputs=" << reflection.numStageInputs
            << ", Outputs=" << reflection.numStageOutputs
            << ", PushConstants=" << reflection.numPushConstants
            << std::endl;
    }

    bool ReflectAndPrint(const std::filesystem::path& outputPath, UMBRA_ShaderType shaderType, UMBRA_ShaderPlatformType platformType)
    {
        std::vector<uint8_t> bytes;
        if (!ReadBinaryFile(outputPath, bytes))
        {
            std::cout << "  Reflection skipped (cannot read output): " << outputPath.generic_string() << std::endl;
            return false;
        }

        try
        {
            if (platformType == UMBRA_SHADER_PLATFORM_TYPE_SPIRV)
            {
                umbra::ShaderReflectionInfo reflection = umbra::ShaderReflection::SPIRVReflect(shaderType, bytes);
                PrintReflectionSummary("SPIRV", reflection);
                return true;
            }

            if (platformType == UMBRA_SHADER_PLATFORM_TYPE_DXIL)
            {
                umbra::ShaderReflectionInfo reflection = umbra::ShaderReflection::DXILReflect(shaderType, bytes);
                PrintReflectionSummary("DXIL", reflection);
                return true;
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << "  Reflection failed for " << outputPath.generic_string() << " -> " << ex.what() << std::endl;
            return false;
        }
        catch (...)
        {
            std::cout << "  Reflection failed for " << outputPath.generic_string() << std::endl;
            return false;
        }

        return false;
    }

    bool CompileAndReflect(const std::filesystem::path& inputPath,
                           const std::filesystem::path& outputDirectory,
                           UMBRA_ShaderType shaderType,
                           UMBRA_ShaderPlatformType platformType)
    {
        umbra::CompilerOptions options = {};
        options.compilerType = UMBRA_SHADER_COMPILER_TYPE_DXC;
        options.platformType = platformType;
        options.filepath = inputPath;
        options.outputFilepath = outputDirectory;
        options.shaderDesc.entryPoint = "main";
        options.shaderDesc.shaderModel = "6_5";
        options.shaderDesc.vulkanVersion = "1.3";
        options.shaderDesc.shaderType = shaderType;
        options.shaderDesc.optLevel = UMBRA_OPT_LEVEL_3;
        options.tRegShift = 0;
        options.sRegShift = 0;
        options.bRegShift = 0;
        options.uRegShift = 0;

        std::vector<uint8_t> output;
        try
        {
            if (IsHlslFile(inputPath))
            {
#if defined(_WIN32)
                std::shared_ptr<umbra::DXCInstance> dxc = umbra::ShaderCompiler::CreateDXCCompiler();
                if (!dxc)
                {
                    std::cout << "Compile (" << UMBRA_ShaderPlatformToString(platformType) << ") "
                              << inputPath.generic_string() << " -> failed (CreateDXCCompiler)" << std::endl;
                    return false;
                }

                output = umbra::ShaderCompiler::CompileDXC(dxc, options);
#else
                std::cout << "Compile (" << UMBRA_ShaderPlatformToString(platformType) << ") "
                          << inputPath.generic_string() << " -> unsupported platform" << std::endl;
                return false;
#endif
            }
            else
            {
                output = umbra::ShaderCompiler::CompileGLSL(options);
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << "Compile (" << UMBRA_ShaderPlatformToString(platformType) << ") "
                      << inputPath.generic_string() << " -> exception: " << ex.what() << std::endl;
            return false;
        }
        catch (...)
        {
            std::cout << "Compile (" << UMBRA_ShaderPlatformToString(platformType) << ") "
                      << inputPath.generic_string() << " -> exception" << std::endl;
            return false;
        }

        const bool ok = !output.empty();
        std::cout << "Compile (" << UMBRA_ShaderPlatformToString(platformType) << ") "
                  << inputPath.generic_string() << " -> " << (ok ? "OK" : "FAILED") << std::endl;
        if (!ok)
        {
            return false;
        }

        const std::filesystem::path outputPath =
            outputDirectory / inputPath.filename().replace_extension(UMBRA_ShaderPlatformExtension(platformType));

        return ReflectAndPrint(outputPath, shaderType, platformType);
    }

    void OnCompilerLog(UMBRA_LogType type, const char* message, void*)
    {
        const char* level = "UNKNOWN";
        if (type == UMBRA_LOG_TYPE_INFO) level = "INFO";
        else if (type == UMBRA_LOG_TYPE_WARNING) level = "WARNING";
        else if (type == UMBRA_LOG_TYPE_ERROR) level = "ERROR";

        std::cout << "[" << level << "] " << (message ? message : "") << std::endl;
    }
}

int main()
{
    umbra::ShaderCompiler::SetLogCallback(OnCompilerLog, nullptr);
    std::cout << "UmbraCompiler version: " << umbra::ShaderCompiler::GetVersion() << std::endl;

    const std::filesystem::path shaderRoot = "Shaders";
    int compiledCount = 0;
    int failedCount = 0;

    if (!std::filesystem::exists(shaderRoot))
    {
        std::cout << "Shaders directory not found: " << shaderRoot.generic_string() << std::endl;
        return 1;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderRoot))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::filesystem::path inputPath = entry.path();
        if (!IsShaderSourceFile(inputPath))
        {
            continue;
        }

        const UMBRA_ShaderType shaderType = DetectShaderTypeFromFilename(inputPath.filename().string());
        const std::filesystem::path outputDir = DetectOutputDirectory(inputPath);
        std::filesystem::create_directories(outputDir);

        if (CompileAndReflect(inputPath, outputDir, shaderType, UMBRA_SHADER_PLATFORM_TYPE_SPIRV))
        {
            compiledCount++;
        }
        else
        {
            failedCount++;
        }

        if (IsHlslFile(inputPath))
        {
            if (CompileAndReflect(inputPath, outputDir, shaderType, UMBRA_SHADER_PLATFORM_TYPE_DXIL))
            {
                compiledCount++;
            }
            else
            {
                failedCount++;
            }
        }
    }

    std::cout << "Compiled: " << compiledCount << ", Failed: " << failedCount << std::endl;
    umbra::ShaderCompiler::ClearLogCallback();

    return (failedCount == 0 && compiledCount > 0) ? 0 : 1;
}
