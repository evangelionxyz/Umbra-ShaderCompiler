// Copyright (c) 2026 Evangelion Manuhutu

#ifndef _SHADER_ENUMS_H
#define _SHADER_ENUMS_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Core shared enums and helper functions used by both C and C++ APIs.
 * These values define shader stage/platform/compiler selection, result codes,
 * and compact format mappings for reflected vertex/stage IO information.
 */

typedef enum UMBRA_LogType
{
    UMBRA_LOG_TYPE_INFO = 0,
    UMBRA_LOG_TYPE_WARNING = 1,
    UMBRA_LOG_TYPE_ERROR = 2
} UMBRA_LogType;

typedef enum UMBRA_ShaderType
{
    UMBRA_SHADER_TYPE_VERTEX = 0,
    UMBRA_SHADER_TYPE_PIXEL = 1,
    UMBRA_SHADER_TYPE_GEOMETRY = 2,
    UMBRA_SHADER_TYPE_COMPUTE = 3,
    UMBRA_SHADER_TYPE_TESSELLATION = 4
} UMBRA_ShaderType;

typedef enum UMBRA_ShaderPlatformType
{
    UMBRA_SHADER_PLATFORM_TYPE_DXBC = 0,
    UMBRA_SHADER_PLATFORM_TYPE_DXIL = 1,
    UMBRA_SHADER_PLATFORM_TYPE_SPIRV = 2
} UMBRA_ShaderPlatformType;

typedef enum UMBRA_ShaderCompilerType
{
    UMBRA_SHADER_COMPILER_TYPE_DXC = 0,
    UMBRA_SHADER_COMPILER_TYPE_FXC = 1,
    UMBRA_SHADER_COMPILER_TYPE_SLANG = 2
} UMBRA_ShaderCompilerType;

typedef enum UMBRA_OptimizationLevel
{
    UMBRA_OPT_LEVEL_0 = 0,
    UMBRA_OPT_LEVEL_1 = 1,
    UMBRA_OPT_LEVEL_2 = 2,
    UMBRA_OPT_LEVEL_3 = 3
} UMBRA_OptimizationLevel;

typedef enum UMBRA_ResultCode
{
    UMBRA_RESULT_OK = 0,
    UMBRA_RESULT_INVALID_ARGUMENT = 1,
    UMBRA_RESULT_UNSUPPORTED_PLATFORM = 2,
    UMBRA_RESULT_COMPILATION_FAILED = 3,
    UMBRA_RESULT_INTERNAL_ERROR = 4
} UMBRA_ResultCode;

typedef enum UMBRA_VertexElementFormat
{
    UMBRA_VERTEX_ELEMENT_FORMAT_INVALID,

    /* 32-bit Signed Integers */
    UMBRA_VERTEX_ELEMENT_FORMAT_INT,
    UMBRA_VERTEX_ELEMENT_FORMAT_INT2,
    UMBRA_VERTEX_ELEMENT_FORMAT_INT3,
    UMBRA_VERTEX_ELEMENT_FORMAT_INT4,

    /* 32-bit Unsigned Integers */
    UMBRA_VERTEX_ELEMENT_FORMAT_UINT,
    UMBRA_VERTEX_ELEMENT_FORMAT_UINT2,
    UMBRA_VERTEX_ELEMENT_FORMAT_UINT3,
    UMBRA_VERTEX_ELEMENT_FORMAT_UINT4,

    /* 32-bit Floats */
    UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT,
    UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT2,
    UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT3,
    UMBRA_VERTEX_ELEMENT_FORMAT_FLOAT4,

    /* 8-bit Signed Integers */
    UMBRA_VERTEX_ELEMENT_FORMAT_BYTE2,
    UMBRA_VERTEX_ELEMENT_FORMAT_BYTE4,

    /* 8-bit Unsigned Integers */
    UMBRA_VERTEX_ELEMENT_FORMAT_UBYTE2,
    UMBRA_VERTEX_ELEMENT_FORMAT_UBYTE4,

    /* 8-bit Signed Normalized */
    UMBRA_VERTEX_ELEMENT_FORMAT_BYTE2_NORM,
    UMBRA_VERTEX_ELEMENT_FORMAT_BYTE4_NORM,

    /* 8-bit Unsigned Normalized */
    UMBRA_VERTEX_ELEMENT_FORMAT_UBYTE2_NORM,
    UMBRA_VERTEX_ELEMENT_FORMAT_UBYTE4_NORM,

    /* 16-bit Signed Integers */
    UMBRA_VERTEX_ELEMENT_FORMAT_SHORT2,
    UMBRA_VERTEX_ELEMENT_FORMAT_SHORT4,

    /* 16-bit Unsigned Integers */
    UMBRA_VERTEX_ELEMENT_FORMAT_USHORT2,
    UMBRA_VERTEX_ELEMENT_FORMAT_USHORT4,

    /* 16-bit Signed Normalized */
    UMBRA_VERTEX_ELEMENT_FORMAT_SHORT2_NORM,
    UMBRA_VERTEX_ELEMENT_FORMAT_SHORT4_NORM,

    /* 16-bit Unsigned Normalized */
    UMBRA_VERTEX_ELEMENT_FORMAT_USHORT2_NORM,
    UMBRA_VERTEX_ELEMENT_FORMAT_USHORT4_NORM,

    /* 16-bit Floats */
    UMBRA_VERTEX_ELEMENT_FORMAT_HALF2,
    UMBRA_VERTEX_ELEMENT_FORMAT_HALF4
} UMBRA_VertexElementFormat;

static const char *UMBRA_ShaderPlatformToString(UMBRA_ShaderPlatformType type)
{
    switch (type)
    {
        case UMBRA_SHADER_PLATFORM_TYPE_DXIL: return "DXIL";
        case UMBRA_SHADER_PLATFORM_TYPE_DXBC: return "DXBC";
        case UMBRA_SHADER_PLATFORM_TYPE_SPIRV: return "SPIRV";
        default: return "Unknown";
    }
}

/* Returns the default output file extension for a target shader platform. */
static const char *UMBRA_ShaderPlatformExtension(UMBRA_ShaderPlatformType type)
{
    switch (type)
    {
    case UMBRA_SHADER_PLATFORM_TYPE_DXIL: return ".dxil";
    case UMBRA_SHADER_PLATFORM_TYPE_DXBC: return ".dxbc";
    case UMBRA_SHADER_PLATFORM_TYPE_SPIRV: return ".spirv";
    default: return "Unknown";
    }
}

/* Returns executable name used by the selected shader compiler backend. */
 static const char *UMBRA_ShaderCompilerExecutablePath(UMBRA_ShaderCompilerType type)
{
    switch (type)
    {
#ifdef _WIN32
    case UMBRA_SHADER_COMPILER_TYPE_DXC: return "dxc.exe";
    case UMBRA_SHADER_COMPILER_TYPE_FXC: return "fxc.exe";
    case UMBRA_SHADER_COMPILER_TYPE_SLANG: return "slangc.exe";
#else
    case UMBRA_SHADER_COMPILER_TYPE_DXC: return "dxc";
    case UMBRA_SHADER_COMPILER_TYPE_FXC: return "fxc";
    case UMBRA_SHADER_COMPILER_TYPE_SLANG: return "slangc";
#endif
    default: return "Unknown";
    }
}

/* Returns shader profile prefix used by HLSL-style targets (vs/ps/gs/cs/ts). */
static const char *UMBRA_ShaderTypeToProfile(UMBRA_ShaderType type)
{
    switch (type)
    {
    case UMBRA_SHADER_TYPE_VERTEX: return "vs";
    case UMBRA_SHADER_TYPE_PIXEL: return "ps";
    case UMBRA_SHADER_TYPE_GEOMETRY: return "gs";
    case UMBRA_SHADER_TYPE_COMPUTE: return "cs";
    case UMBRA_SHADER_TYPE_TESSELLATION: return "ts";
    default: return "invalid";
    }
}

/* Returns a human-readable stage name for logs and diagnostics output. */
static const char* UMBRA_GetShaderTypeString(UMBRA_ShaderType type)
{
    switch (type)
    {
        case UMBRA_SHADER_TYPE_VERTEX: return "Vertex";
        case UMBRA_SHADER_TYPE_PIXEL: return "Pixel";
        case UMBRA_SHADER_TYPE_GEOMETRY: return "Geometry";
        case UMBRA_SHADER_TYPE_COMPUTE: return "Compute";
        default: return "Invalid";
    }
}

#ifdef __cplusplus
}
#endif

#endif