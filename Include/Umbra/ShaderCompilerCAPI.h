// Copyright (c) 2026 Evangelion Manuhutu

#ifndef UMBRA_SHADER_COMPILER_CAPI_H
#define UMBRA_SHADER_COMPILER_CAPI_H

#pragma once

#if defined(_WIN32)
    #if defined(UMBRACOMPILER_BUILD_SHARED)
        #define UMBRACOMPILER_CAPI __declspec(dllexport)
    #else
        #define UMBRACOMPILER_CAPI __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define UMBRACOMPILER_CAPI __attribute__((visibility("default")))
#else
    #define UMBRACOMPILER_CAPI
#endif

#include "ShaderBase.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C API surface for the Umbra shader compiler.
 * - Compile shader files to target bytecode formats.
 * - Reflect SPIR-V and DXIL binaries into plain C structs.
 * - Release reflection allocations via UmbraCompiler_FreeReflectionInfo.
 */

/* Input parameters for one compile invocation. */
typedef struct UmbraCompileRequest
{
    const char* inputPath;
    const char* outputDirectory;
    const char* entryPoint;
    const char* shaderModel;
    const char* vulkanVersion;
    const char* vulkanMemoryLayout;
    UMBRA_ShaderType shaderType;
    UMBRA_ShaderPlatformType platformType;
    UMBRA_OptimizationLevel optimizationLevel;
    int warningsAreErrors;
    int allResourcesBound;
    int stripReflection;
    int matrixRowMajor;
    int hlsl2021;
    int embedPdb;
    int pdb;
    int verbose;
    uint32_t tRegShift;
    uint32_t sRegShift;
    union
    {
        uint32_t bRegShift;
        uint32_t rRegShift;
    };
    uint32_t uRegShift;
} UmbraCompileRequest;

/* Reflected vertex attribute metadata. */
typedef struct UmbraVertexAttribute
{
    char* name;
    UMBRA_VertexElementFormat format;
    uint32_t location;
    uint32_t bufferIndex;
    uint32_t offset;
    uint32_t elementStride;
} UmbraVertexAttribute;

/* Generic reflected resource (UBO/image/buffer/sampler). */
typedef struct UmbraShaderResourceInfo
{
    char* name;
    uint32_t id;
    uint32_t location;
    uint32_t set;
    uint32_t binding;
    uint32_t count;
} UmbraShaderResourceInfo;

/* Reflected stage input/output entry metadata. */
typedef struct UmbraShaderStageIOInfo
{
    char* name;
    uint32_t id;
    uint32_t location;
    UMBRA_VertexElementFormat format;
    uint32_t vecSize;
    uint32_t columns;
} UmbraShaderStageIOInfo;

/* Reflected push constant metadata. */
typedef struct UmbraShaderPushConstantInfo
{
    char* name;
    uint32_t size;
} UmbraShaderPushConstantInfo;

/* Aggregate reflection result for one shader binary. */
typedef struct UmbraShaderReflectionInfo
{
    UMBRA_ShaderType shaderType;

    size_t numUniformBuffers;
    size_t numSamplers;
    size_t numStorageTextures;
    size_t numStorageBuffers;
    size_t numSeparateSamplers;
    size_t numSeparateImages;
    size_t numPushConstants;
    size_t numStageInputs;
    size_t numStageOutputs;

    UmbraShaderResourceInfo* uniformBuffers;
    UmbraShaderResourceInfo* sampledImages;
    UmbraShaderResourceInfo* storageImages;
    UmbraShaderResourceInfo* storageBuffers;
    UmbraShaderResourceInfo* separateSamplers;
    UmbraShaderResourceInfo* separateImages;
    UmbraShaderPushConstantInfo* pushConstants;
    UmbraShaderStageIOInfo* stageInputs;
    UmbraShaderStageIOInfo* stageOutputs;
    UmbraVertexAttribute* vertexAttributes;
    size_t vertexAttributeCount;
} UmbraShaderReflectionInfo;

/* Callback signature for compiler/reflection log forwarding. */
typedef void(*UmbraLogCallback)(UMBRA_LogType type, const char* message, void* userData);

/* Returns project version string. */
UMBRACOMPILER_CAPI const char* UmbraCompiler_GetVersion(void);

/* Installs or clears callback-based logging integration. */
UMBRACOMPILER_CAPI void UmbraCompiler_SetLogCallback(UmbraLogCallback callback, void* userData);

/* Compiles an input shader file to request->platformType output. */
UMBRACOMPILER_CAPI UMBRA_ResultCode UmbraCompiler_Compile(const UmbraCompileRequest* request);

/* Reflects SPIR-V words and fills outReflectionInfo. */
UMBRACOMPILER_CAPI UMBRA_ResultCode UmbraCompiler_ReflectSPIRV(const uint32_t* spirvData, size_t sizeInBytes, UMBRA_ShaderType shaderType, UmbraShaderReflectionInfo* outReflectionInfo);

/* Reflects DXIL bytes and fills outReflectionInfo. */
UMBRACOMPILER_CAPI UMBRA_ResultCode UmbraCompiler_ReflectDXIL(const uint8_t* dxilData, size_t sizeInBytes, UMBRA_ShaderType shaderType, UmbraShaderReflectionInfo* outReflectionInfo);

/* Releases heap allocations stored in UmbraShaderReflectionInfo. */
UMBRACOMPILER_CAPI void UmbraCompiler_FreeReflectionInfo(UmbraShaderReflectionInfo* reflectionInfo);

#ifdef __cplusplus
}
#endif

#endif
