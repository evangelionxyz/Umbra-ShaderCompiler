// Copyright (c) 2026 Evangelion Manuhutu

#pragma once
#ifndef _SHADER_COMPILER_H
#define _SHADER_COMPILER_H

#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
    #include <d3dcompiler.h>
    #include <d3dcommon.h>
    #include <combaseapi.h>
    #include <wrl/client.h>
    
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

// DXC compiler API — available on Windows and Linux via the Vulkan SDK.
// On Windows, dxcapi.h lives in the Windows SDK / Vulkan SDK include path.
// On Linux,   dxcapi.h is supplied by the Vulkan SDK ($VULKAN_SDK/include/dxc/dxcapi.h).
#ifdef _WIN32
    #include <dxcapi.h>
#elif __linux__
    #include <dxc/dxcapi.h>
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
    // Cross-platform DXC wide-string type.
    //   Windows: WCHAR == wchar_t  (UTF-16, 2 bytes, from Windows SDK)
    //   Linux:   WCHAR == char16_t (UTF-16, 2 bytes, from DXC WinAdapter.h via dxcapi.h)
    // Use DxcString wherever a string must be passed to a DXC API function.
    using DxcString = std::basic_string<WCHAR>;

    // Converts narrow (ASCII/Latin-1) string to DxcString for DXC arguments.
    static DxcString AnsiToDxcWide(const std::string& s)
    {
        DxcString result;
        result.reserve(s.size());
        for (unsigned char c : s)
            result.push_back(static_cast<WCHAR>(c));
        return result;
    }

    // Converts a filesystem path to a DxcString for use with DXC file-loading APIs.
    static DxcString PathToDxcWide(const std::filesystem::path& p)
    {
#ifdef _WIN32
        std::wstring ws = p.wstring();
        return DxcString(ws.begin(), ws.end());
#else
        std::u16string u16 = p.u16string();
        return DxcString(u16.begin(), u16.end());
#endif
    }

    // Converts a wchar_t wide-string literal (e.g. from DXC_ARG_* macros or L"...") to DxcString.
    //   Windows (sizeof(wchar_t)==2): char-by-char copy (same bit width).
    //   Linux   (sizeof(wchar_t)==4): truncates lower 16 bits — safe for all ASCII DXC args.
    static DxcString WcharToDxcString(const wchar_t* ws)
    {
        if (!ws) return {};
        DxcString result;
        for (const wchar_t* p = ws; *p; ++p)
            result.push_back(static_cast<WCHAR>(*p));
        return result;
    }

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
    // Converts key=value define strings to DXC-compatible macro pairs (Windows / FXC only).
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
#endif

    // Parses a string with command line options into a vector of DxcStrings, one per option.
    // Options are separated by spaces and may be quoted with "double quotes".
    // Backslash (\) means the next character is inserted literally into the output.
    // Cross-platform: works on Windows and Linux.
    static void TokenizeCompilerOptions(const char* in, std::vector<DxcString>& out)
    {
        DxcString current;
        bool quotes = false;
        bool escape = false;
        const char* ptr = in;
        while (char ch = *ptr++)
        {
            if (escape)
            {
                current.push_back(static_cast<WCHAR>(ch));
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
                current.push_back(static_cast<WCHAR>(ch));
            }
        }

        if (!current.empty())
        {
            out.push_back(current);
        }
    }

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

    static bool IsSpace(char ch) 
    { 
        return strchr(" \t\r\n", ch) != nullptr; 
    }

    static bool HasRepeatingSpace(char a, char b)
    {
        return (a == b) && a == ' ';
    }

    // Cross-platform DXC COM smart-pointer alias.
    //   Windows: Microsoft::WRL::ComPtr<T>  (from wrl/client.h, included above)
    //   Linux:   CComPtr<T>                 (from DXC's WinAdapter.h via dxcapi.h)
    // Use DxcComPtr<T> everywhere instead of raw WRL or CComPtr to stay portable.
#ifdef _WIN32
    template<typename T>
    using DxcComPtr = Microsoft::WRL::ComPtr<T>;
#else
    template<typename T>
    using DxcComPtr = CComPtr<T>;
#endif

    // DXC COM objects reused across compilation invocations.
    struct DXCInstance
    {
        DxcComPtr<IDxcCompiler3> compiler;
        DxcComPtr<IDxcUtils> utils;
    };

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