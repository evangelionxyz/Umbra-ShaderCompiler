// Copyright (c) 2026 Evangelion Manuhutu

#include "Umbra/ShaderCompiler.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include <spirv_cross/spirv_cross_c.h>
#include <shaderc/shaderc.h>

namespace umbra
{
    const uint32_t SPIRV_SPACES_NUM = 8;

    namespace
    {
        // Global callback state used by DispatchLog.
        LogCallback g_logCallback = nullptr;
        void* g_logUserData = nullptr;

        // Centralized typed logging dispatch for compiler/reflection diagnostics.
        void DispatchLog(UMBRA_LogType type, const std::string& message)
        {
            if (g_logCallback)
            {
                g_logCallback(type, message.c_str(), g_logUserData);
            }
        }

        // Converts wide strings (DXC messages on Windows) to UTF-8.
        std::string WStringToUtf8(const std::wstring& text)
        {
            if (text.empty())
            {
                return {};
            }

#ifdef _WIN32
            int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (sizeNeeded <= 0)
            {
                return {};
            }

            std::string output(static_cast<size_t>(sizeNeeded), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), output.data(), sizeNeeded, nullptr, nullptr);
            return output;
#else
            return std::string(text.begin(), text.end());
#endif
        }

        // Reads text source files (HLSL/GLSL) before compilation.
        std::string ReadTextFile(const std::filesystem::path& filepath)
        {
            std::ifstream file(filepath, std::ios::in | std::ios::binary);
            if (!file)
            {
                return {};
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        bool ReadTextFile(const std::filesystem::path& filepath, std::string& output)
        {
            std::ifstream file(filepath, std::ios::in | std::ios::binary);
            if (!file)
            {
                return false;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            output = buffer.str();
            return true;
        }

        struct ShadercCompileContext
        {
            shaderc_compiler* compiler = nullptr;
            shaderc_compile_options* compileOptions = nullptr;
            shaderc_compilation_result* compilationResult = nullptr;

            ~ShadercCompileContext()
            {
                if (compilationResult)
                {
                    shaderc_result_release(compilationResult);
                }

                if (compileOptions)
                {
                    shaderc_compile_options_release(compileOptions);
                }

                if (compiler)
                {
                    shaderc_compiler_release(compiler);
                }
            }
        };

        struct ShadercIncludeContext
        {
            std::filesystem::path rootShaderPath;
            std::vector<std::filesystem::path> includeDirectories;
        };

        struct ShadercIncludeResultStorage
        {
            std::string sourceName;
            std::string content;
        };

        std::filesystem::path ResolveShadercIncludePath(const ShadercIncludeContext* context,
            const char* requestedSource,
            shaderc_include_type includeType,
            const char* requestingSource)
        {
            if (!context || !requestedSource || requestedSource[0] == '\0')
            {
                return {};
            }

            std::filesystem::path requestedPath(requestedSource);
            std::error_code ec;
            if (requestedPath.is_absolute() && std::filesystem::exists(requestedPath, ec) && !ec)
            {
                return std::filesystem::weakly_canonical(requestedPath, ec);
            }

            std::vector<std::filesystem::path> roots;
            roots.reserve(context->includeDirectories.size() + 2);

            if (includeType == shaderc_include_type_relative)
            {
                if (requestingSource && requestingSource[0] != '\0')
                {
                    roots.push_back(std::filesystem::path(requestingSource).parent_path());
                }
                else
                {
                    roots.push_back(context->rootShaderPath.parent_path());
                }
            }

            roots.push_back(context->rootShaderPath.parent_path());
            for (const std::filesystem::path& includeDir : context->includeDirectories)
            {
                roots.push_back(includeDir);
            }

            for (const std::filesystem::path& root : roots)
            {
                const std::filesystem::path candidate = root / requestedPath;
                ec.clear();
                if (std::filesystem::exists(candidate, ec) && !ec)
                {
                    return std::filesystem::weakly_canonical(candidate, ec);
                }
            }

            return {};
        }

        shaderc_include_result* ShadercIncludeResolver(void* userData,
            const char* requestedSource,
            int type,
            const char* requestingSource,
            size_t /*includeDepth*/)
        {
            auto* context = static_cast<ShadercIncludeContext*>(userData);
            auto* storage = new ShadercIncludeResultStorage();
            auto* result = new shaderc_include_result();

            std::filesystem::path resolvedPath = ResolveShadercIncludePath(context,
                requestedSource,
                static_cast<shaderc_include_type>(type),
                requestingSource);

            if (!resolvedPath.empty() && ReadTextFile(resolvedPath, storage->content))
            {
                storage->sourceName = resolvedPath.generic_string();
            }
            else
            {
                storage->sourceName = requestedSource ? requestedSource : "<unknown include>";
                storage->content = "Failed to resolve include '" + storage->sourceName + "'";
                if (requestingSource && requestingSource[0] != '\0')
                {
                    storage->content += " requested from '" + std::string(requestingSource) + "'";
                }
                storage->content += ".";
            }

            result->source_name = storage->sourceName.c_str();
            result->source_name_length = storage->sourceName.size();
            result->content = storage->content.c_str();
            result->content_length = storage->content.size();
            result->user_data = storage;

            return result;
        }

        void ShadercIncludeResultReleaser(void* /*userData*/, shaderc_include_result* includeResult)
        {
            if (!includeResult)
            {
                return;
            }

            auto* storage = static_cast<ShadercIncludeResultStorage*>(includeResult->user_data);
            delete storage;
            delete includeResult;
        }

        void EmitIgnoredGLSLOptionsWarnings(const CompilerOptions& options)
        {
            std::vector<std::string> ignored;

            if (options.pdb)
            {
                ignored.push_back("pdb");
            }

            if (options.embedPdb)
            {
                ignored.push_back("embedPdb");
            }

            if (options.allResourcesBound)
            {
                ignored.push_back("allResourcesBound");
            }

            if (options.matrixRowMajor)
            {
                ignored.push_back("matrixRowMajor");
            }

            if (options.hlsl2021)
            {
                ignored.push_back("hlsl2021");
            }

            if (options.stripReflection)
            {
                ignored.push_back("stripReflection");
            }

            if (options.noRegShifts
                || options.tRegShift != 0
                || options.sRegShift != 128
                || options.bRegShift != 256
                || options.uRegShift != 384)
            {
                ignored.push_back("register shift options (t/s/b/u/noRegShifts)");
            }

            if (!options.compilerOptions.empty())
            {
                ignored.push_back("compilerOptions");
            }

            if (!ignored.empty())
            {
                std::string message = "GLSL(shaderc): ignored option(s): ";
                for (size_t i = 0; i < ignored.size(); ++i)
                {
                    if (i > 0)
                    {
                        message += ", ";
                    }
                    message += ignored[i];
                }
                message += ".";
                DispatchLog(UMBRA_LOG_TYPE_WARNING, message);
            }
        }

#ifdef _WIN32
    // Maps D3D reflection component type to project vertex element format.
        UMBRA_VertexElementFormat UMBRA_Map3DComponent(D3D_REGISTER_COMPONENT_TYPE componentType, uint32_t elementCount)
        {
            switch (componentType)
            {
                case D3D_REGISTER_COMPONENT_FLOAT32:
                switch (elementCount)
                {
                    case 1: return UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT;
                    case 2: return UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT2;
                    case 3: return UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT3;
                    case 4: return UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT4;
                    default: break;
                }
                break;
                case D3D_REGISTER_COMPONENT_SINT32:
                switch (elementCount)
                {
                    case 1: return UMBRA_VERTEX_ELEMENT_FORMAT_INT;
                    case 2: return UMBRA_VERTEX_ELEMENT_FORMAT_INT2;
                    case 3: return UMBRA_VERTEX_ELEMENT_FORMAT_INT3;
                    case 4: return UMBRA_VERTEX_ELEMENT_FORMAT_INT4;
                    default: break;
                }
                break;
                case D3D_REGISTER_COMPONENT_UINT32:
                switch (elementCount)
                {
                    case 1: return UMBRA_VERTEX_ELEMENT_FORMAT_UINT;
                    case 2: return UMBRA_VERTEX_ELEMENT_FORMAT_UINT2;
                    case 3: return UMBRA_VERTEX_ELEMENT_FORMAT_UINT3;
                    case 4: return UMBRA_VERTEX_ELEMENT_FORMAT_UINT4;
                    default: break;
                }
                break;
            }
            
            return UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
        }
#endif

    // Maps SPIRV-Cross types to project vertex element format.
        UMBRA_VertexElementFormat UMBRA_MapSpvcType(spvc_type typeHandle)
        {
            if (!typeHandle)
            {
                return UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
            }

            const spvc_basetype baseType = spvc_type_get_basetype(typeHandle);
            const uint32_t vecSize = spvc_type_get_vector_size(typeHandle);
            const uint32_t columns = spvc_type_get_columns(typeHandle);

            if (columns != 1)
            {
                return UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
            }

            if (baseType == SPVC_BASETYPE_FP32)
            {
                switch (vecSize)
                {
                case 1: return UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT;
                case 2: return UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT2;
                case 3: return UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT3;
                case 4: return UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT4;
                default: return UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
                }
            }

            if (baseType == SPVC_BASETYPE_INT32)
            {
                switch (vecSize)
                {
                case 1: return UMBRA_VERTEX_ELEMENT_FORMAT_INT;
                case 2: return UMBRA_VERTEX_ELEMENT_FORMAT_INT2;
                case 3: return UMBRA_VERTEX_ELEMENT_FORMAT_INT3;
                case 4: return UMBRA_VERTEX_ELEMENT_FORMAT_INT4;
                default: return UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
                }
            }

            if (baseType == SPVC_BASETYPE_UINT32)
            {
                switch (vecSize)
                {
                case 1: return UMBRA_VERTEX_ELEMENT_FORMAT_UINT;
                case 2: return UMBRA_VERTEX_ELEMENT_FORMAT_UINT2;
                case 3: return UMBRA_VERTEX_ELEMENT_FORMAT_UINT3;
                case 4: return UMBRA_VERTEX_ELEMENT_FORMAT_UINT4;
                default: return UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
                }
            }

            return UMBRA_VERTEX_ELEMENT_FORMAT_INVALID;
        }

        // Maps project shader type to shaderc stage kind.
        shaderc_shader_kind UMBRA_ShaderToShaderCKind(UMBRA_ShaderType type)
        {
            switch (type)
            {
            case UMBRA_SHADER_TYPE_VERTEX: return shaderc_glsl_vertex_shader;
            case UMBRA_SHADER_TYPE_PIXEL: return shaderc_glsl_fragment_shader;
            case UMBRA_SHADER_TYPE_GEOMETRY: return shaderc_glsl_geometry_shader;
            case UMBRA_SHADER_TYPE_COMPUTE: return shaderc_glsl_compute_shader;
            case UMBRA_SHADER_TYPE_TESSELLATION: return shaderc_glsl_infer_from_source;
            default: return shaderc_glsl_infer_from_source;
            }
        }

        // Maps project optimization enum to shaderc optimization level.
        shaderc_optimization_level UMBRA_ShaderToShaderCOptLevel(UMBRA_OptimizationLevel level)
        {
            switch (level)
            {
            case UMBRA_OPT_LEVEL_0: return shaderc_optimization_level_zero;
            case UMBRA_OPT_LEVEL_1:
            case UMBRA_OPT_LEVEL_2:
            case UMBRA_OPT_LEVEL_3:
            default:
                return shaderc_optimization_level_performance;
            }
        }

        // Maps Vulkan version string to shaderc environment target.
        shaderc_env_version UMBRA_ShaderToVulkanEnvVersion(const char *version)
        {
            if (strcmp(version, "1.0") == 0) return shaderc_env_version_vulkan_1_0;
            if (strcmp(version, "1.1") == 0) return shaderc_env_version_vulkan_1_1;
            if (strcmp(version, "1.2") == 0) return shaderc_env_version_vulkan_1_2;
            return shaderc_env_version_vulkan_1_3;
        }
    }

    // Public API: register global logging callback.
    void ShaderCompiler::SetLogCallback(LogCallback callback, void* userData)
    {
        g_logCallback = callback;
        g_logUserData = userData;
    }

    // Public API: clear global logging callback.
    void ShaderCompiler::ClearLogCallback()
    {
        g_logCallback = nullptr;
        g_logUserData = nullptr;
    }

    DataOutputContext::DataOutputContext(const char* file, bool textMode)
    {
        stream = fopen(file, textMode ? "w" : "wb");
        if (!stream)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, std::string("Cannot open file for writing: ") + file);
        }
    }

    DataOutputContext::~DataOutputContext()
    {
        if (stream)
        {
            fclose(stream);
            stream = nullptr;
        }
    }

    bool DataOutputContext::WriteDataAsText(const void* data, size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            uint8_t value = ((const uint8_t*)data)[i];

            if (m_lineLength > 128)
            {
                fprintf(stream, "\n    ");
                m_lineLength = 0;
            }

            fprintf(stream, "%u,", value);

            if (value < 10)
                m_lineLength += 3;
            else if (value < 100)
                m_lineLength += 4;
            else
                m_lineLength += 5;
        }

        return true;
    }

    void DataOutputContext::WriteTextPreamble(const char* shaderName, const std::string& combinedDefines)
    {
        fprintf(stream, "// {%s}\n", combinedDefines.c_str());
        fprintf(stream, "const uint8_t %s[] = {", shaderName);
    }

    void DataOutputContext::WriteTextEpilog()
    {
        fprintf(stream, "\n};\n");
    }

    bool DataOutputContext::WriteDataAsBinary(const void* data, size_t size)
    {
        if (size == 0)
            return true;

        return fwrite(data, size, 1, stream) == 1;
    }

    // For use as a callback in "WriteFileHeader" and "WritePermutation" functions
    bool DataOutputContext::WriteDataAsTextCallback(const void* data, size_t size, void* context)
    {
        return ((DataOutputContext*)context)->WriteDataAsText(data, size);
    }

    bool DataOutputContext::WriteDataAsBinaryCallback(const void* data, size_t size, void* context)
    {
        return ((DataOutputContext*)context)->WriteDataAsBinary(data, size);
    }

    std::shared_ptr<DXCInstance> ShaderCompiler::CreateDXCCompiler()
    {
        std::shared_ptr<DXCInstance> instance = std::make_shared<DXCInstance>();
        HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&instance->compiler));
        if (FAILED(hr))
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "Failed to create IDxcCompiler3 instance. HRESULT=" + std::to_string((long long)hr)
                + " " + std::system_category().message((int)hr));
            return nullptr;
        }

        hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&instance->utils));
        if (FAILED(hr))
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "Failed to create IDxcUtils instance. HRESULT=" + std::to_string((long long)hr)
                + " " + std::system_category().message((int)hr));
            return nullptr;
        }

        DispatchLog(UMBRA_LOG_TYPE_INFO, "DXC compiler initialized.");

        return instance;
    }

    std::vector<uint8_t> ShaderCompiler::CompileDXC(std::shared_ptr<DXCInstance> instance, const CompilerOptions &options)
    {
        using namespace Microsoft::WRL;

        static const wchar_t* dxcOptimizationLevelRemap[] =
        {
            // Note: if you're getting errors like "error C2065: 'DXC_ARG_SKIP_OPTIMIZATIONS': undeclared identifier" here,
            // please update the Windows SDK to at least version 10.0.20348.0.
            DXC_ARG_SKIP_OPTIMIZATIONS,
            DXC_ARG_OPTIMIZATION_LEVEL1,
            DXC_ARG_OPTIMIZATION_LEVEL2,
            DXC_ARG_OPTIMIZATION_LEVEL3,
        };

        // Gather SPIRV register shifts once
        static const wchar_t* dxcRegShiftArgs[] =
        {
            L"-fvk-t-shift",
            L"-fvk-s-shift",
            L"-fvk-b-shift",
            L"-fvk-u-shift",
        };

        std::vector<std::wstring> regShifts;
        for (uint32_t reg = 0; reg < 4; reg++)
        {
            for (uint32_t space = 0; space < SPIRV_SPACES_NUM; space++)
            {
                wchar_t buf[64];
                regShifts.push_back(dxcRegShiftArgs[reg]);

                swprintf(buf, std::size(buf), L"%u", (&options.tRegShift)[reg]);
                regShifts.push_back(std::wstring(buf));

                swprintf(buf, std::size(buf), L"%u", space);
                regShifts.push_back(std::wstring(buf));
            }
        }

        // Compile shader
        std::wstring wsourceFile = options.filepath.wstring();
        std::vector<uint8_t> resultCode;
        
        ComPtr<IDxcBlobEncoding> sourceBlob;
        HRESULT hr = instance->utils->LoadFile(wsourceFile.c_str(), nullptr, &sourceBlob);
        if (SUCCEEDED(hr))
        {
            std::vector<std::wstring> args;
            args.reserve(16 + (options.defines.size()
                + options.defines.size()
                + options.includeDirectories.size()) * 2
                + (options.platformType == UMBRA_SHADER_PLATFORM_TYPE_SPIRV ? regShifts.size()
                + options.spirvExtensions.size() : 0));

            args.push_back(wsourceFile); // Source file
            args.push_back(L"-T"); // Profile

            std::string shaderProfile = std::string(UMBRA_ShaderTypeToProfile(options.shaderDesc.shaderType));
            args.push_back(AnsiToWide(shaderProfile + "_" + options.shaderDesc.shaderModel));
            args.push_back(L"-E"); // Entry Point
            args.push_back(AnsiToWide(options.shaderDesc.entryPoint));

            // Defines
            for (const std::string& define : options.defines)
            {
                args.push_back(L"-D");
                args.push_back(AnsiToWide(define));
            }

            // Include directories
            for (const std::filesystem::path& path : options.includeDirectories)
            {
                args.push_back(L"-I");
                args.push_back(path.wstring());
            }

            // Arguments
            args.push_back(dxcOptimizationLevelRemap[static_cast<uint32_t>(options.shaderDesc.optLevel)]);

            uint32_t shaderModelIndex = (options.shaderDesc.shaderModel[0] - '0') * 10 + (options.shaderDesc.shaderModel[2] - '0');
            if (shaderModelIndex >= 62)
                args.push_back(L"-enable-16bit-types");

            if (options.warningsAreErrors)
                args.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);

            if (options.allResourcesBound)
                args.push_back(DXC_ARG_ALL_RESOURCES_BOUND);

            if (options.matrixRowMajor)
                args.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);

            if (options.hlsl2021)
            {
                args.push_back(L"-HV");
                args.push_back(L"2021");
            }

            if (options.embedPdb)
            {
                args.push_back(L"-Qembed_debug");
            }

            if (options.platformType == UMBRA_SHADER_PLATFORM_TYPE_SPIRV)
            {
                args.push_back(L"-spirv");
                args.push_back(std::wstring(L"-fspv-target-env=vulkan") + AnsiToWide(options.shaderDesc.vulkanVersion));

                if (!options.shaderDesc.vulkanMemoryLayout.empty())
                {
                    args.push_back(std::wstring(L"-fvk-use-") + AnsiToWide(options.shaderDesc.vulkanMemoryLayout) + std::wstring(L"-layout"));
                }

                for (const std::string& ext : options.spirvExtensions)
                {
                    args.push_back(std::wstring(L"-fspv-extension=") + AnsiToWide(ext));
                }

                for (const std::wstring& arg : regShifts)
                {
                    args.push_back(arg);
                }
            }
            else // Not supported by SPIRV Gen
            {
                if (options.stripReflection)
                {
                    args.push_back(L"-Qstrip_reflect");
                }
            }

            for (std::string const& opts : options.compilerOptions)
            {
                TokenizeCompilerOptions(opts.c_str(), args);
            }

            // Debug output
            if (options.verbose)
            {
                std::wstringstream cmd;
                for (const std::wstring& arg : args)
                {
                    cmd << arg;
                    cmd << L" ";
                }

                const std::wstring wcmd = cmd.str();
                DispatchLog(UMBRA_LOG_TYPE_WARNING, WStringToUtf8(wcmd));
            }

            // Now that args are finalized, get their C-string pointer into vector
            std::vector<const wchar_t*> argPointers;
            argPointers.reserve(args.size());
            for (const std::wstring& arg : args)
            {
                argPointers.push_back(arg.c_str());
            }

            // Compile the shader
            DxcBuffer sourceBuffer = {};
            sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
            sourceBuffer.Size = sourceBlob->GetBufferSize();

            ComPtr<IDxcIncludeHandler> pDefaultIncludeHandler;
            instance->utils->CreateDefaultIncludeHandler(&pDefaultIncludeHandler);

            ComPtr<IDxcBlob> shaderBlob;
            ComPtr<IDxcBlobEncoding> errorBlob;
            ComPtr<IDxcResult> dxcResult;
            hr = instance->compiler->Compile(&sourceBuffer, argPointers.data(), (uint32_t)argPointers.size(), pDefaultIncludeHandler.Get(), IID_PPV_ARGS(&dxcResult));

            if (SUCCEEDED(hr))
            {
                dxcResult->GetStatus(&hr);
            }

            if (dxcResult)
            {
                dxcResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
                dxcResult->GetErrorBuffer(&errorBlob);
            }

            bool isSucceeded = SUCCEEDED(hr) && shaderBlob;

            if (!isSucceeded)
            {
                std::string errorText = "Shader compilation failed.";
                if (errorBlob && errorBlob->GetBufferPointer() && errorBlob->GetBufferSize() > 0)
                {
                    errorText += " ";
                    errorText += std::string((const char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize());
                }
                DispatchLog(UMBRA_LOG_TYPE_ERROR, errorText);

                if (options.platformType == UMBRA_SHADER_PLATFORM_TYPE_SPIRV
                    && errorText.find("SPIR-V CodeGen not available") != std::string::npos)
                {
                    DispatchLog(UMBRA_LOG_TYPE_ERROR,
                        "DXC runtime does not include SPIR-V codegen. Ensure Vulkan SDK dxcompiler.dll/dxil.dll are loaded (copy next to the executable or adjust PATH)."
                    );
                }
            }

            // Dump PDB
            if (isSucceeded && options.pdb)
            {
                ComPtr<IDxcBlob> pdb;
                ComPtr<IDxcBlobUtf16> pdbName;
                if (SUCCEEDED(dxcResult->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pdb), &pdbName)))
                {
                    std::wstring file = options.filepath.parent_path().wstring() + L"/" + L"PDB" + L"/" + std::wstring(pdbName->GetStringPointer());
                    FILE* fp = _wfopen(file.c_str(), L"wb");
                    if (fp)
                    {
                        fwrite(pdb->GetBufferPointer(), pdb->GetBufferSize(), 1, fp);
                        fclose(fp);
                    }
                }
            }

            // Dump output
            if (isSucceeded)
            {
                std::string outputExtension = UMBRA_ShaderPlatformExtension(options.platformType);
                std::filesystem::path parentPath = options.filepath.parent_path();
                if (!options.outputFilepath.empty())
                {
                    parentPath = options.outputFilepath;
                }

                std::filesystem::path filename = parentPath / options.filepath.filename().replace_extension(outputExtension);

                size_t bufferSize = shaderBlob->GetBufferSize();
                const void* bufferPtr = shaderBlob->GetBufferPointer();
                resultCode.resize(bufferSize);
                std::memcpy(resultCode.data(), bufferPtr, bufferSize);

                DumpShader(options, resultCode, filename.generic_string());
                DispatchLog(UMBRA_LOG_TYPE_INFO, "Compiled shader: " + filename.generic_string());
            }
        }

        return resultCode;
    }

    std::vector<uint8_t> ShaderCompiler::CompileGLSL(const CompilerOptions& options)
    {
        std::vector<uint8_t> resultCode;

        if (options.platformType != UMBRA_SHADER_PLATFORM_TYPE_SPIRV)
        {
            DispatchLog(UMBRA_LOG_TYPE_WARNING, "GLSL compilation currently supports SPIRV output only.");
            return resultCode;
        }

        std::string source = ReadTextFile(options.filepath);
        if (source.empty())
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "Failed to read GLSL file: " + options.filepath.generic_string());
            return resultCode;
        }

        EmitIgnoredGLSLOptionsWarnings(options);

        // Initialize shaderc
        ShadercCompileContext shadercContext = {};
        shadercContext.compiler = shaderc_compiler_initialize();
        shadercContext.compileOptions = shaderc_compile_options_initialize();

        if (!shadercContext.compiler || !shadercContext.compileOptions)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "Failed to initialize shaderc compiler/options.");
            return resultCode;
        }

        ShadercIncludeContext includeContext = {};
        includeContext.rootShaderPath = options.filepath;
        includeContext.includeDirectories = options.includeDirectories;

        shaderc_compile_options_set_include_callbacks(shadercContext.compileOptions,
            ShadercIncludeResolver,
            ShadercIncludeResultReleaser,
            &includeContext);

        shaderc_compile_options_set_source_language(shadercContext.compileOptions, shaderc_source_language_glsl);
        shaderc_compile_options_set_target_env(shadercContext.compileOptions, shaderc_target_env_vulkan, UMBRA_ShaderToVulkanEnvVersion(options.shaderDesc.vulkanVersion.c_str()));
        shaderc_compile_options_set_optimization_level(shadercContext.compileOptions, UMBRA_ShaderToShaderCOptLevel(options.shaderDesc.optLevel));

        if (options.warningsAreErrors)
        {
            shaderc_compile_options_set_warnings_as_errors(shadercContext.compileOptions);
        }

        for (const std::string& define : options.defines)
        {
            size_t equalPos = define.find('=');
            if (equalPos == std::string::npos)
            {
                shaderc_compile_options_add_macro_definition(shadercContext.compileOptions, define.data(), define.size(), NULL, 0u);
            }
            else
            {
                std::string name = define.substr(0, equalPos);
                std::string value = define.substr(equalPos + 1);
                shaderc_compile_options_add_macro_definition(shadercContext.compileOptions, name.data(), name.size(), value.data(), value.size());
            }
        }

        if (options.verbose)
        {
            DispatchLog(UMBRA_LOG_TYPE_INFO, "Compiling GLSL: " + options.filepath.generic_string());
        }

        std::string outFilenameStr = options.filepath.generic_string();
        shadercContext.compilationResult = shaderc_compile_into_spv(shadercContext.compiler, source.c_str(), source.size(),
            UMBRA_ShaderToShaderCKind(options.shaderDesc.shaderType), outFilenameStr.c_str(),
            options.shaderDesc.entryPoint.c_str(), shadercContext.compileOptions);

        if (!shadercContext.compilationResult)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "GLSL compilation failed: shaderc returned no result.");
            return resultCode;
        }

        if (shadercContext.compilationResult)
        {
            shaderc_compilation_status compilationStatus = shaderc_result_get_compilation_status(shadercContext.compilationResult);
            const size_t numWarnings = shaderc_result_get_num_warnings(shadercContext.compilationResult);
            if (compilationStatus != shaderc_compilation_status_success)
            {
                std::string errorMessage = shaderc_result_get_error_message(shadercContext.compilationResult);
                DispatchLog(UMBRA_LOG_TYPE_ERROR, "GLSL compilation failed: " + errorMessage);
                return resultCode;
            }

            if (numWarnings > 0)
            {
                std::string errorMessage = shaderc_result_get_error_message(shadercContext.compilationResult);
                DispatchLog(UMBRA_LOG_TYPE_WARNING, errorMessage);
            }
        }
        
        // const size_t wordCount = static_cast<size_t>(compileResult.cend() - compileResult.cbegin());
        const size_t byteCount = shaderc_result_get_length(shadercContext.compilationResult);
        resultCode.resize(byteCount);
        std::memcpy(resultCode.data(), shaderc_result_get_bytes(shadercContext.compilationResult), resultCode.size());

        std::string outputExtension = UMBRA_ShaderPlatformExtension(options.platformType);
        std::filesystem::path parentPath = options.filepath.parent_path();
        if (!options.outputFilepath.empty())
        {
            parentPath = options.outputFilepath;
        }

        std::filesystem::path filename = parentPath / options.filepath.filename().replace_extension(outputExtension);
        DumpShader(options, resultCode, filename.generic_string());
        DispatchLog(UMBRA_LOG_TYPE_INFO, "Compiled GLSL shader: " + filename.generic_string());

        return resultCode;
    }

    const char* ShaderCompiler::GetVersion()
    {
        return "1.0.0";
    }

    void ShaderCompiler::DumpShader(const CompilerOptions& options, std::vector<uint8_t>& shaderCode, const std::string& outputPath)
    {
        std::string shaderPlatformStr = UMBRA_ShaderPlatformToString(options.platformType);
        if (options.binary || options.binaryBlob || (options.headerBlob))
        {
            DataOutputContext context(outputPath.c_str(), false);
            if (!context.stream)
            {
                return;
            }

            context.WriteDataAsBinary(shaderCode.data(), shaderCode.size());
            DispatchLog(UMBRA_LOG_TYPE_INFO, "Writing binary " +shaderPlatformStr+ ": " + outputPath);
        }

        if (options.header || options.headerBlob)
        {
            std::string headerOutput = outputPath + ".h"; // .h extension
        
            DataOutputContext context(headerOutput.c_str(), true);
            if (!context.stream)
                return;

            std::string shaderName = options.filepath.filename().generic_string();

            context.WriteTextPreamble(shaderName.c_str(), options.shaderDesc.combinedDefines);
            context.WriteDataAsText(shaderCode.data(), shaderCode.size());
            context.WriteTextEpilog();

            DispatchLog(UMBRA_LOG_TYPE_INFO, "Writing header [" + shaderPlatformStr + "]: " + headerOutput);
        }
    }

    ShaderReflectionInfo ShaderReflection::SPIRVReflect(UMBRA_ShaderType type, const std::vector<uint8_t>& shaderCode)
    {
        ShaderReflectionInfo info = {};
        info.shaderType = type;

        if (shaderCode.size() % sizeof(uint32_t) != 0)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "SPIRV reflection failed: shader blob size is not aligned to 4 bytes.");
            return info;
        }

        spvc_context context = nullptr;
        spvc_parsed_ir ir = nullptr;
        spvc_compiler compiler = nullptr;
        spvc_resources resources = nullptr;

        if (spvc_context_create(&context) != SPVC_SUCCESS)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "SPIRV reflection failed: could not create SPIRV-Cross context.");
            return info;
        }

        const uint32_t* words = reinterpret_cast<const uint32_t*>(shaderCode.data());
        const size_t wordCount = shaderCode.size() / sizeof(uint32_t);

        if (spvc_context_parse_spirv(context, words, wordCount, &ir) != SPVC_SUCCESS)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "SPIRV reflection failed: could not parse SPIRV blob.");
            spvc_context_destroy(context);
            return info;
        }

        if (spvc_context_create_compiler(context, SPVC_BACKEND_NONE, ir, SPVC_CAPTURE_MODE_COPY, &compiler) != SPVC_SUCCESS)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "SPIRV reflection failed: could not create SPIRV-Cross compiler.");
            spvc_context_destroy(context);
            return info;
        }

        if (spvc_compiler_create_shader_resources(compiler, &resources) != SPVC_SUCCESS)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "SPIRV reflection failed: could not create shader resources.");
            spvc_context_destroy(context);
            return info;
        }

        auto collectBindings = [&](spvc_resource_type resourceType, std::vector<ShaderResourceInfo>& outResources)
        {
            const spvc_reflected_resource* list = nullptr;
            size_t count = 0;
            if (spvc_resources_get_resource_list_for_type(resources, resourceType, &list, &count) != SPVC_SUCCESS)
            {
                return;
            }

            outResources.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                ShaderResourceInfo item = {};
                item.name = list[i].name ? list[i].name : "";
                item.id = list[i].id;
                item.location = spvc_compiler_get_decoration(compiler, list[i].id, SpvDecorationLocation);
                item.set = spvc_compiler_get_decoration(compiler, list[i].id, SpvDecorationDescriptorSet);
                item.binding = spvc_compiler_get_decoration(compiler, list[i].id, SpvDecorationBinding);
                item.count = 1;
                outResources.push_back(std::move(item));
            }
        };

        collectBindings(SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, info.uniformBuffers);
        collectBindings(SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, info.sampledImages);
        collectBindings(SPVC_RESOURCE_TYPE_STORAGE_IMAGE, info.storageImages);
        collectBindings(SPVC_RESOURCE_TYPE_STORAGE_BUFFER, info.storageBuffers);
        collectBindings(SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS, info.separateSamplers);
        collectBindings(SPVC_RESOURCE_TYPE_SEPARATE_IMAGE, info.separateImages);

        info.numUniformBuffers = info.uniformBuffers.size();
        info.numSamplers = info.sampledImages.size();
        info.numStorageTextures = info.storageImages.size();
        info.numStorageBuffers = info.storageBuffers.size();
        info.numSeparateSamplers = info.separateSamplers.size();
        info.numSeparateImages = info.separateImages.size();

        {
            const spvc_reflected_resource* pushConstantList = nullptr;
            size_t pushConstantCount = 0;
            if (spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_PUSH_CONSTANT, &pushConstantList, &pushConstantCount) == SPVC_SUCCESS)
            {
                info.pushConstants.reserve(pushConstantCount);
                for (size_t i = 0; i < pushConstantCount; ++i)
                {
                    ShaderPushConstantInfo pushConstant = {};
                    pushConstant.name = pushConstantList[i].name ? pushConstantList[i].name : "";

                    spvc_type typeHandle = spvc_compiler_get_type_handle(compiler, pushConstantList[i].base_type_id);
                    size_t declaredSize = 0;
                    if (typeHandle && spvc_compiler_get_declared_struct_size(compiler, typeHandle, &declaredSize) == SPVC_SUCCESS)
                    {
                        pushConstant.size = static_cast<uint32_t>(declaredSize);
                    }

                    info.pushConstants.push_back(std::move(pushConstant));
                }
            }
        }
        info.numPushConstants = info.pushConstants.size();

        auto collectStageIO = [&](spvc_resource_type stageType, std::vector<ShaderStageIOInfo>& outIO)
        {
            const spvc_reflected_resource* list = nullptr;
            size_t count = 0;
            if (spvc_resources_get_resource_list_for_type(resources, stageType, &list, &count) != SPVC_SUCCESS)
            {
                return;
            }

            outIO.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                ShaderStageIOInfo io = {};
                io.name = list[i].name ? list[i].name : "";
                io.id = list[i].id;
                io.location = spvc_compiler_get_decoration(compiler, list[i].id, SpvDecorationLocation);

                spvc_type typeHandle = spvc_compiler_get_type_handle(compiler, list[i].type_id);
                if (typeHandle)
                {
                    io.vecSize = spvc_type_get_vector_size(typeHandle);
                    io.columns = spvc_type_get_columns(typeHandle);
                    io.format = UMBRA_MapSpvcType(typeHandle);
                }

                outIO.push_back(std::move(io));
            }

            std::sort(outIO.begin(), outIO.end(), [](const ShaderStageIOInfo& a, const ShaderStageIOInfo& b) {
                return a.location < b.location;
            });
        };

        collectStageIO(SPVC_RESOURCE_TYPE_STAGE_INPUT, info.stageInputs);
        collectStageIO(SPVC_RESOURCE_TYPE_STAGE_OUTPUT, info.stageOutputs);

        info.numStageInputs = info.stageInputs.size();
        info.numStageOutputs = info.stageOutputs.size();

        if (type == UMBRA_SHADER_TYPE_VERTEX)
        {
            uint32_t offset = 0;
            for (const ShaderStageIOInfo& input : info.stageInputs)
            {
                if (input.format == UMBRA_VERTEX_ELEMENT_FORMAT_INVALID)
                {
                    DispatchLog(UMBRA_LOG_TYPE_WARNING, "SPIRV reflection: unsupported vertex attribute format at location " + std::to_string(input.location));
                    continue;
                }

                const uint32_t componentSize = 4;
                const uint32_t elementCount = input.vecSize > 0 ? input.vecSize : 1;
                const uint32_t attributeSize = componentSize * elementCount;

                VertexAttribute attribute = {};
                attribute.name = input.name;
                attribute.format = input.format;
                attribute.location = input.location;
                attribute.bufferIndex = 0;
                attribute.offset = offset;

                offset += attributeSize;
                info.vertexAttributes.push_back(std::move(attribute));
            }

            const uint32_t stride = offset;
            for (VertexAttribute& attribute : info.vertexAttributes)
            {
                attribute.elementStride = stride;
            }
        }

        DispatchLog(UMBRA_LOG_TYPE_INFO, "SPIRV reflection complete: " + std::string(UMBRA_GetShaderTypeString(type))
            + " | UBO=" + std::to_string(info.numUniformBuffers)
            + " Sampled=" + std::to_string(info.numSamplers)
            + " StorageTex=" + std::to_string(info.numStorageTextures)
            + " StorageBuf=" + std::to_string(info.numStorageBuffers)
            + " Inputs=" + std::to_string(info.numStageInputs)
            + " Outputs=" + std::to_string(info.numStageOutputs));

        spvc_context_destroy(context);
        return info;
    }

    ShaderReflectionInfo ShaderReflection::DXILReflect(UMBRA_ShaderType type, const std::vector<uint8_t>& shaderCode)
    {
        ShaderReflectionInfo info = {};
        info.shaderType = type;

#ifdef _WIN32
        if (shaderCode.empty() || shaderCode.size() < 4)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "DXIL reflection failed: shader blob is empty or too small.");
            return info;
        }

        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        HRESULT result = E_FAIL;

        Microsoft::WRL::ComPtr<IDxcUtils> utils;
        Microsoft::WRL::ComPtr<IDxcContainerReflection> containerReflection;
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
        result = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils.GetAddressOf()));
        if (SUCCEEDED(result))
        {
            result = utils->CreateBlob(shaderCode.data(), static_cast<UINT32>(shaderCode.size()), CP_ACP, &blob);
        }
        if (SUCCEEDED(result))
        {
            result = DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(containerReflection.GetAddressOf()));
        }
        if (SUCCEEDED(result))
        {
            result = containerReflection->Load(blob.Get());
        }
        if (SUCCEEDED(result))
        {
            UINT32 partIndex = 0;
            if (SUCCEEDED(containerReflection->FindFirstPartKind(DXC_PART_DXIL, &partIndex)))
            {
                result = containerReflection->GetPartReflection(partIndex, IID_PPV_ARGS(reflection.GetAddressOf()));
            }
            else
            {
                result = E_FAIL;
            }
        }

        if (FAILED(result) || !reflection)
        {
            result = D3DReflect(shaderCode.data(), shaderCode.size(), IID_PPV_ARGS(reflection.GetAddressOf()));
        }

        if (FAILED(result) || !reflection)
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "DXIL reflection failed. HRESULT=" + std::to_string(static_cast<uint32_t>(result)));
            return info;
        }

        D3D12_SHADER_DESC shaderDesc = {};
        result = reflection->GetDesc(&shaderDesc);
        if (FAILED(result))
        {
            DispatchLog(UMBRA_LOG_TYPE_ERROR, "DXIL reflection failed while reading shader description. HRESULT=" + std::to_string(static_cast<uint32_t>(result)));
            return info;
        }

        DispatchLog(UMBRA_LOG_TYPE_INFO, std::string("DXIL reflection: ") + UMBRA_GetShaderTypeString(type));

        info.uniformBuffers.reserve(shaderDesc.ConstantBuffers);
        for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
        {
            ID3D12ShaderReflectionConstantBuffer* cbuffer = reflection->GetConstantBufferByIndex(i);
            if (!cbuffer)
            {
                continue;
            }

            D3D12_SHADER_BUFFER_DESC cbufferDesc = {};
            if (FAILED(cbuffer->GetDesc(&cbufferDesc)))
            {
                continue;
            }

            ShaderResourceInfo resource = {};
            resource.name = cbufferDesc.Name ? cbufferDesc.Name : "";
            resource.id = i;

            for (UINT b = 0; b < shaderDesc.BoundResources; ++b)
            {
                D3D12_SHADER_INPUT_BIND_DESC bindDesc = {};
                if (SUCCEEDED(reflection->GetResourceBindingDesc(b, &bindDesc)) && bindDesc.Type == D3D_SIT_CBUFFER && bindDesc.Name && resource.name == bindDesc.Name)
                {
                    resource.binding = bindDesc.BindPoint;
                    resource.set = bindDesc.Space;
                    resource.count = bindDesc.BindCount;
                    break;
                }
            }

            info.uniformBuffers.push_back(std::move(resource));
        }

        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc = {};
            if (FAILED(reflection->GetResourceBindingDesc(i, &bindDesc)))
            {
                continue;
            }

            ShaderResourceInfo resource = {};
            resource.name = bindDesc.Name ? bindDesc.Name : "";
            resource.id = i;
            resource.binding = bindDesc.BindPoint;
            resource.set = bindDesc.Space;
            resource.count = bindDesc.BindCount;

            switch (bindDesc.Type)
            {
            case D3D_SIT_TEXTURE:
                info.sampledImages.push_back(std::move(resource));
                break;
            case D3D_SIT_SAMPLER:
                info.separateSamplers.push_back(std::move(resource));
                break;
            case D3D_SIT_UAV_RWTYPED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            case D3D_SIT_UAV_FEEDBACKTEXTURE:
                info.storageBuffers.push_back(std::move(resource));
                break;
            default:
                break;
            }
        }

        info.numUniformBuffers = info.uniformBuffers.size();
        info.numSamplers = info.sampledImages.size();
        info.numStorageTextures = info.storageImages.size();
        info.numStorageBuffers = info.storageBuffers.size();
        info.numSeparateSamplers = info.separateSamplers.size();
        info.numSeparateImages = info.separateImages.size();
        info.numPushConstants = info.pushConstants.size();

        struct InputAttribute
        {
            UINT registerIndex;
            std::string semanticName;
            UINT semanticIndex;
            UINT mask;
            D3D_REGISTER_COMPONENT_TYPE componentType;
        };

        std::vector<InputAttribute> inputs;
        if (type == UMBRA_SHADER_TYPE_VERTEX)
        {
            inputs.reserve(shaderDesc.InputParameters);
        }

        for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC paramDesc = {};
            if (FAILED(reflection->GetInputParameterDesc(i, &paramDesc)))
            {
                continue;
            }

            ShaderStageIOInfo stageInput = {};
            stageInput.name = paramDesc.SemanticName ? paramDesc.SemanticName : "";
            if (paramDesc.SemanticIndex > 0)
            {
                stageInput.name += std::to_string(paramDesc.SemanticIndex);
            }
            stageInput.id = i;
            stageInput.location = paramDesc.Register;
            stageInput.columns = 1;

            UINT mask = paramDesc.Mask;
            uint32_t elementCount = 0;
            while (mask != 0)
            {
                elementCount += (mask & 1u) ? 1u : 0u;
                mask >>= 1;
            }
            stageInput.vecSize = (elementCount > 0) ? elementCount : 1u;
            stageInput.format = UMBRA_Map3DComponent(paramDesc.ComponentType, stageInput.vecSize);
            info.stageInputs.push_back(stageInput);

            if (type == UMBRA_SHADER_TYPE_VERTEX)
            {
                inputs.push_back({ paramDesc.Register, paramDesc.SemanticName ? paramDesc.SemanticName : "", paramDesc.SemanticIndex, paramDesc.Mask, paramDesc.ComponentType });
            }
        }

        for (UINT i = 0; i < shaderDesc.OutputParameters; ++i)
        {
            D3D12_SIGNATURE_PARAMETER_DESC paramDesc = {};
            if (FAILED(reflection->GetOutputParameterDesc(i, &paramDesc)))
            {
                continue;
            }

            ShaderStageIOInfo stageOutput = {};
            stageOutput.name = paramDesc.SemanticName ? paramDesc.SemanticName : "";
            if (paramDesc.SemanticIndex > 0)
            {
                stageOutput.name += std::to_string(paramDesc.SemanticIndex);
            }
            stageOutput.id = i;
            stageOutput.location = paramDesc.Register;
            stageOutput.columns = 1;

            UINT mask = paramDesc.Mask;
            uint32_t elementCount = 0;
            while (mask != 0)
            {
                elementCount += (mask & 1u) ? 1u : 0u;
                mask >>= 1;
            }
            stageOutput.vecSize = (elementCount > 0) ? elementCount : 1u;
            stageOutput.format = UMBRA_Map3DComponent(paramDesc.ComponentType, stageOutput.vecSize);
            info.stageOutputs.push_back(stageOutput);
        }

        std::sort(info.stageInputs.begin(), info.stageInputs.end(), [](const ShaderStageIOInfo& a, const ShaderStageIOInfo& b) {
            return a.location < b.location;
        });
        std::sort(info.stageOutputs.begin(), info.stageOutputs.end(), [](const ShaderStageIOInfo& a, const ShaderStageIOInfo& b) {
            return a.location < b.location;
        });

        info.numStageInputs = info.stageInputs.size();
        info.numStageOutputs = info.stageOutputs.size();

        if (type == UMBRA_SHADER_TYPE_VERTEX && !inputs.empty())
        {
            std::sort(inputs.begin(), inputs.end(), [](const InputAttribute& a, const InputAttribute& b) {
                return a.registerIndex < b.registerIndex;
            });

            auto countMask = [](UINT mask)
            {
                uint32_t count = 0;
                while (mask != 0)
                {
                    count += (mask & 0x1u) ? 1u : 0u;
                    mask >>= 1;
                }
                return (count > 0) ? count : 1u;
            };

            uint32_t offset = 0;
            for (const auto& input : inputs)
            {
                const uint32_t elementCount = countMask(input.mask);
                const UMBRA_VertexElementFormat elementFormat = UMBRA_Map3DComponent(input.componentType, elementCount);
                if (elementFormat == UMBRA_VERTEX_ELEMENT_FORMAT_INVALID)
                {
                    DispatchLog(UMBRA_LOG_TYPE_WARNING, "DXIL reflection: unsupported vertex input format for semantic " + input.semanticName);
                    continue;
                }

                std::string name = input.semanticName;
                if (input.semanticIndex > 0)
                {
                    name += std::to_string(input.semanticIndex);
                }

                VertexAttribute attribute = {};
                attribute.name = name;
                attribute.format = elementFormat;
                attribute.location = static_cast<uint32_t>(input.registerIndex);
                attribute.offset = offset;
                attribute.bufferIndex = 0;

                const uint32_t componentSize = 4;
                const uint32_t attributeSize = componentSize * elementCount;
                offset += attributeSize;
                info.vertexAttributes.push_back(std::move(attribute));
            }

            const uint32_t stride = offset;
            for (auto& attribute : info.vertexAttributes)
            {
                attribute.elementStride = stride;
            }
        }

        DispatchLog(UMBRA_LOG_TYPE_INFO, "DXIL reflection complete: " + std::string(UMBRA_GetShaderTypeString(type))
            + " | UBO=" + std::to_string(info.numUniformBuffers)
            + " Sampled=" + std::to_string(info.numSamplers)
            + " StorageBuf=" + std::to_string(info.numStorageBuffers)
            + " Inputs=" + std::to_string(info.numStageInputs)
            + " Outputs=" + std::to_string(info.numStageOutputs));
#else
        DispatchLog(UMBRA_LOG_TYPE_WARNING, "DXIL reflection is only available on Windows platform");
#endif

        return info;
    }


}