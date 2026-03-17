// Copyright (c) 2026 Evangelion Manuhutu

#ifndef _SHADER_COMPILER_H
#define _SHADER_COMPILER_H

#pragma once

#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#   include <d3dcompiler.h>
#   include <d3dcommon.h>
#   include <combaseapi.h>
#   include <wrl/client.h>
#   include <dxcapi.h>
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#endif

#include "ShaderBase.h"

#if defined(_WIN32)
    #if defined(UMBRACOMPILER_BUILD_SHARED)
        #define UMBRACOMPILER_API __declspec(dllexport)
    #else
        #define UMBRACOMPILER_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define UMBRACOMPILER_API __attribute__((visibility("default")))
#else
    #define UMBRACOMPILER_API
#endif

namespace umbra
{
    // Compiler log callback used by C++ and bridged by the C API.
    using LogCallback = void(*)(UMBRA_LogType type, const char* message, void* userData);
    
    // Vertex attribute metadata extracted during reflection.
    struct VertexAttribute
    {
        std::string name;
        UMBRA_VertexElementFormat format = UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
        uint32_t location = 0;
        uint32_t bufferIndex = 0;
        uint32_t offset = 0;
        uint32_t elementStride = 0;
    };

    // Generic descriptor-like resource information from reflection output.
    struct ShaderResourceInfo
    {
        std::string name;
        uint32_t id = 0;
        uint32_t location = 0;
        uint32_t set = 0;
        uint32_t binding = 0;
        uint32_t count = 1;
    };

    // Stage input/output metadata (location/format/vector width).
    struct ShaderStageIOInfo
    {
        std::string name;
        uint32_t id = 0;
        uint32_t location = 0;
        UMBRA_VertexElementFormat format = UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
        uint32_t vecSize = 0;
        uint32_t columns = 0;
    };

    // Push constant metadata (name/size) extracted from shader bytecode.
    struct ShaderPushConstantInfo
    {
        std::string name;
        uint32_t size = 0;
    };

    // Unified reflection model returned by both SPIR-V and DXIL reflection paths.
    struct ShaderReflectionInfo
    {
        UMBRA_ShaderType shaderType = UMBRA_SHADER_TYPE_VERTEX;

        size_t numUniformBuffers = 0;
        size_t numSamplers = 0;
        size_t numStorageTextures = 0;
        size_t numStorageBuffers = 0;
        size_t numSeparateSamplers = 0;
        size_t numSeparateImages = 0;
        size_t numPushConstants = 0;
        size_t numStageInputs = 0;
        size_t numStageOutputs = 0;

        std::vector<ShaderResourceInfo> uniformBuffers;
        std::vector<ShaderResourceInfo> sampledImages;
        std::vector<ShaderResourceInfo> storageImages;
        std::vector<ShaderResourceInfo> storageBuffers;
        std::vector<ShaderResourceInfo> separateSamplers;
        std::vector<ShaderResourceInfo> separateImages;
        std::vector<ShaderPushConstantInfo> pushConstants;
        std::vector<ShaderStageIOInfo> stageInputs;
        std::vector<ShaderStageIOInfo> stageOutputs;
        std::vector<VertexAttribute> vertexAttributes;
    };

#ifdef _WIN32
    // Converts key=value define strings to DXC-compatible macro pairs.
    static void TokenizeDefineStrings(std::vector<std::string>& in, std::vector<D3D_SHADER_MACRO>& out)
    {
        if (in.empty())
            return;

        out.reserve(out.size() + in.size());
        for (const std::string& defineString : in)
        {
            D3D_SHADER_MACRO& define = out.emplace_back();
            char* s = (char*)defineString.c_str(); // IMPORTANT: "defineString" gets split into tokens divided by '\0'
            define.Name = strtok(s, "=");
            define.Definition = strtok(nullptr, "=");
        }
    }

    // Parses a string with command line options into a vector of wstring, one wstring per option.
    // Options are separated by spaces and may be quoted with "double quotes".
    // Backslash (\) means the next character is inserted literally into the output.
    static void TokenizeCompilerOptions(const char* in, std::vector<std::wstring>& out)
    {
        std::wstring current;
        bool quotes = false;
        bool escape = false;
        const char* ptr = in;
        while (char ch = *ptr++)
        {
            if (escape)
            {
                current.push_back(wchar_t(ch));
                escape = false;
                continue;
            }

            if (ch == ' ' && !quotes)
            {
                if (!current.empty())
                    out.push_back(current);
                current.clear();
            }
            else if (ch == '\\')
            {
                escape = true;
            }
            else if (ch == '"')
            {
                quotes = !quotes;
            }
            else
            {
                current.push_back(wchar_t(ch));
            }
        }

        if (!current.empty())
        {
            out.push_back(current);
        }
    }
#endif

    // Utility hash narrowing helper used for stable 32-bit IDs.
    static uint32_t HashToUint(size_t hash)
    { 
        return uint32_t(hash) ^ (uint32_t(hash >> 32));
    }

    // Converts a filesystem path to normalized preferred string representation.
    static std::string PathToString(std::filesystem::path path)
    {
        return path.lexically_normal().make_preferred().string();
    }

    static std::wstring AnsiToWide(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());
    }

    static bool IsSpace(char ch) 
    { 
        return strchr(" \t\r\n", ch) != nullptr; 
    }

    static bool HasRepeatingSpace(char a, char b)
    {
        return (a == b) && a == ' ';
    }

#ifdef _WIN32
    // DXC COM objects reused across compilation invocations.
    struct DXCInstance
    {
        Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
        Microsoft::WRL::ComPtr<IDxcUtils> utils;
    };
#endif

    // Per-shader compilation description.
    struct ShaderDesc
    {
        std::string entryPoint = "main";
        std::string shaderModel = "6_5";
        std::string vulkanVersion = "1.3";
        std::string vulkanMemoryLayout;
        std::string combinedDefines;
        UMBRA_ShaderType shaderType;
        UMBRA_OptimizationLevel optLevel = UMBRA_OPT_LEVEL_3;
    };

    // Full compiler configuration for a single compile operation.
    struct CompilerOptions
    {
        UMBRA_ShaderCompilerType compilerType;
        UMBRA_ShaderPlatformType platformType;
        std::filesystem::path filepath;
        std::filesystem::path outputFilepath;

        void AddDefine(const std::string& define) { defines.push_back(define); }
        void AddSPIRVExtension(const std::string& ext) { spirvExtensions.push_back(ext); }
        void AddCompilerOptions(const std::string& opt) { compilerOptions.push_back(opt); }

        std::vector<std::filesystem::path> includeDirectories;
        std::vector<std::filesystem::path> relaxedIncludes;
        std::vector<std::string> spirvExtensions = { "SPV_EXT_descriptor_indexing", "KHR" };
        std::vector<std::string> compilerOptions;
        std::vector<std::string> defines;

        uint32_t tRegShift = 0; // must be first (or change "DxcCompile" code)
        uint32_t sRegShift = 128;
        uint32_t bRegShift = 256;
        uint32_t uRegShift = 384;

        ShaderDesc shaderDesc;

        bool serial = false;
        bool flatten = false;
        bool help = false;
        bool binary = true;
        bool header = false;
        bool binaryBlob = true;
        bool headerBlob = false;
        bool continueOnError = false;
        bool warningsAreErrors = false;
        bool allResourcesBound = false;
        bool pdb = false;
        bool embedPdb = false;
        bool stripReflection = false;
        bool matrixRowMajor = false;
        bool hlsl2021 = false;
        bool verbose = false;
        bool colorize = true;
        bool useAPI = false;
        bool slangHlsl = false;
        bool noRegShifts = false;
        int retryCount = 10; // default 10 retries for compilation task sub-process failures
    };

    // Helper for writing text or binary shader outputs to disk.
    class DataOutputContext
    {
    public:
        FILE* stream = nullptr;

        DataOutputContext(const char* file, bool textMode);
        ~DataOutputContext();
        bool WriteDataAsText(const void* data, size_t size);
        void WriteTextPreamble(const char* shaderName, const std::string& combinedDefines);
        void WriteTextEpilog();
        bool WriteDataAsBinary(const void* data, size_t size);
        static bool WriteDataAsTextCallback(const void* data, size_t size, void* context);
        static bool WriteDataAsBinaryCallback(const void* data, size_t size, void* context);

    private:
        uint32_t m_lineLength = 129;
    };

    class UMBRACOMPILER_API ShaderCompiler
    {
    public:
        // Registers global logging callback for compiler operations.
        static void SetLogCallback(LogCallback callback, void* userData = nullptr);

        // Clears active logging callback.
        static void ClearLogCallback();

        // Creates DXC toolchain instance (Windows).
        static std::shared_ptr<DXCInstance> CreateDXCCompiler();

        // Compiles HLSL source using DXC for DXIL/SPIR-V targets.
        static std::vector<uint8_t> CompileDXC(std::shared_ptr<DXCInstance> instance, const CompilerOptions &options);

        // Compiles GLSL source to SPIR-V using shaderc.
        static std::vector<uint8_t> CompileGLSL(const CompilerOptions &options);

        // Writes compiled output bytes to disk according to options.
        static void DumpShader(const CompilerOptions &options, std::vector<uint8_t> &shaderCode, const std::string &outputPath);

        // Returns project version string.
        static const char* GetVersion();
    };

    // Reflection API for inspecting compiled shader bytecode.
    class UMBRACOMPILER_API ShaderReflection
    {
    public:
        // Reflects SPIR-V binary into ShaderReflectionInfo.
        static ShaderReflectionInfo SPIRVReflect(UMBRA_ShaderType type, const std::vector<uint8_t> &shaderCode);

        // Reflects DXIL binary into ShaderReflectionInfo.
        static ShaderReflectionInfo DXILReflect(UMBRA_ShaderType type, const std::vector<uint8_t>& shaderCode);
    };
}

#endif