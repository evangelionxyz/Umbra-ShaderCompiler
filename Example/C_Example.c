// Copyright (c) 2026 Evangelion Manuhutu

#include "Umbra/ShaderCompilerCAPI.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <Windows.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <errno.h>
#endif

static const char* ToLogTypeString(UMBRA_LogType type)
{
    switch (type)
    {
    case UMBRA_LOG_TYPE_INFO: return "INFO";
    case UMBRA_LOG_TYPE_WARNING: return "WARNING";
    case UMBRA_LOG_TYPE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static void OnCompilerLog(UMBRA_LogType type, const char* message, void* userData)
{
    (void)userData;
    printf("[%s] %s\n", ToLogTypeString(type), message);
}

static int EndsWith(const char* text, const char* suffix)
{
    size_t textLen = strlen(text);
    size_t suffixLen = strlen(suffix);
    if (textLen < suffixLen)
    {
        return 0;
    }

    return strcmp(text + (textLen - suffixLen), suffix) == 0;
}

static int IsShaderSourceFile(const char* filename)
{
    return EndsWith(filename, ".hlsl") || EndsWith(filename, ".glsl");
}

static int IsHlslFile(const char* filename)
{
    return EndsWith(filename, ".hlsl");
}

static int IsPathSeparator(char ch)
{
    return ch == '/' || ch == '\\';
}

static int ContainsPathSegment(const char* path, const char* segment)
{
    size_t pathLen = strlen(path);
    size_t segLen = strlen(segment);
    if (pathLen < segLen)
    {
        return 0;
    }

    for (size_t i = 0; i + segLen <= pathLen; ++i)
    {
        if ((i == 0 || IsPathSeparator(path[i - 1])) && strncmp(path + i, segment, segLen) == 0)
        {
            size_t end = i + segLen;
            if (end == pathLen || IsPathSeparator(path[end]))
            {
                return 1;
            }
        }
    }

    return 0;
}

static int EnsureDirectory(const char* path)
{
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL) != 0)
    {
        return 1;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
#else
    if (mkdir(path, 0755) == 0)
    {
        return 1;
    }
    return errno == EEXIST;
#endif
}

static int EnsureDirectoryRecursive(const char* path)
{
    char temp[1024] = {0};
    size_t len = strlen(path);
    if (len >= sizeof(temp))
    {
        return 0;
    }

    snprintf(temp, sizeof(temp), "%s", path);

    for (size_t i = 1; i < len; ++i)
    {
        if (IsPathSeparator(temp[i]))
        {
            char saved = temp[i];
            temp[i] = '\0';
            if (strlen(temp) > 0)
            {
                EnsureDirectory(temp);
            }
            temp[i] = saved;
        }
    }

    return EnsureDirectory(temp);
}

static const char* DetectOutputDirectory(const char* inputPath)
{
    if (ContainsPathSegment(inputPath, "HLSL"))
    {
        return "Shaders/Compiled/HSLSL";
    }

    if (ContainsPathSegment(inputPath, "GLSL"))
    {
        return "Shaders/Compiled/GLSL";
    }

    return "Shaders/Compiled/Misc";
}

static UMBRA_ShaderType DetectShaderTypeFromFilename(const char* filename)
{
    if (strstr(filename, ".vertex.") != NULL)
        return UMBRA_SHADER_TYPE_VERTEX;
    if (strstr(filename, ".pixel.") != NULL)
        return UMBRA_SHADER_TYPE_PIXEL;
    if (strstr(filename, ".geometry.") != NULL)
        return UMBRA_SHADER_TYPE_GEOMETRY;
    if (strstr(filename, ".compute.") != NULL)
        return UMBRA_SHADER_TYPE_COMPUTE;
    if (strstr(filename, ".tessellation.") != NULL)
        return UMBRA_SHADER_TYPE_TESSELLATION;

    return UMBRA_SHADER_TYPE_VERTEX;
}

static const char* GetFileNameFromPath(const char* path)
{
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* fileName = slash;
    if (backslash != NULL && (fileName == NULL || backslash > fileName))
    {
        fileName = backslash;
    }

    return (fileName == NULL) ? path : (fileName + 1);
}

static int BuildOutputPath(char* outputPath, size_t outputPathSize, const char* outputDirectory, const char* inputPath, UMBRA_ShaderPlatformType platformType)
{
    const char* inputFileName = GetFileNameFromPath(inputPath);
    char baseName[256] = {0};
    snprintf(baseName, sizeof(baseName), "%s", inputFileName);

    char* dot = strrchr(baseName, '.');
    if (dot != NULL)
    {
        *dot = '\0';
    }

    return snprintf(outputPath, outputPathSize, "%s/%s%s", outputDirectory, baseName, UMBRA_ShaderPlatformExtension(platformType)) > 0;
}

static int ReadBinaryFile(const char* filePath, unsigned char** outData, size_t* outSize)
{
    FILE* file = fopen(filePath, "rb");
    long fileSize;
    size_t readSize;
    unsigned char* data;

    *outData = NULL;
    *outSize = 0;

    if (file == NULL)
    {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }

    fileSize = ftell(file);
    if (fileSize <= 0)
    {
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return 0;
    }

    data = (unsigned char*)malloc((size_t)fileSize);
    if (data == NULL)
    {
        fclose(file);
        return 0;
    }

    readSize = fread(data, 1, (size_t)fileSize, file);
    fclose(file);

    if (readSize != (size_t)fileSize)
    {
        free(data);
        return 0;
    }

    *outData = data;
    *outSize = readSize;
    return 1;
}

static void PrintReflectionSummary(const char* label, const UmbraShaderReflectionInfo* reflection)
{
    printf("  [%s Reflection] type=%s, UBO=%zu, Samplers=%zu, StorageTex=%zu, StorageBuf=%zu, Inputs=%zu, Outputs=%zu, PushConstants=%zu\n",
        label,
        UMBRA_GetShaderTypeString(reflection->shaderType),
        reflection->numUniformBuffers,
        reflection->numSamplers,
        reflection->numStorageTextures,
        reflection->numStorageBuffers,
        reflection->numStageInputs,
        reflection->numStageOutputs,
        reflection->numPushConstants);
}

static int ReflectAndPrint(const char* outputPath, UMBRA_ShaderType shaderType, UMBRA_ShaderPlatformType platformType)
{
    unsigned char* binaryData = NULL;
    size_t binarySize = 0;
    UmbraShaderReflectionInfo reflectionInfo;
    UMBRA_ResultCode result;

    if (!ReadBinaryFile(outputPath, &binaryData, &binarySize))
    {
        printf("  Reflection skipped (cannot read output): %s\n", outputPath);
        return 0;
    }

    memset(&reflectionInfo, 0, sizeof(reflectionInfo));

    if (platformType == UMBRA_SHADER_PLATFORM_TYPE_SPIRV)
    {
        if ((binarySize % 4) != 0)
        {
            printf("  Reflection failed for %s (SPIRV size must be 4-byte aligned).\n", outputPath);
            free(binaryData);
            return 0;
        }
        result = UmbraCompiler_ReflectSPIRV((const uint32_t*)binaryData, binarySize, shaderType, &reflectionInfo);
        if (result == UMBRA_RESULT_OK)
        {
            PrintReflectionSummary("SPIRV", &reflectionInfo);
        }
    }
    else
    {
        result = UmbraCompiler_ReflectDXIL(binaryData, binarySize, shaderType, &reflectionInfo);
        if (result == UMBRA_RESULT_OK)
        {
            PrintReflectionSummary("DXIL", &reflectionInfo);
        }
    }

    if (result != UMBRA_RESULT_OK)
    {
        printf("  Reflection failed for %s -> %d\n", outputPath, (int)result);
        free(binaryData);
        return 0;
    }

    UmbraCompiler_FreeReflectionInfo(&reflectionInfo);
    free(binaryData);
    return 1;
}

static int CompileAndReflect(const char* inputPath, const char* outputDirectory, UMBRA_ShaderType shaderType, UMBRA_ShaderPlatformType platformType)
{
    UmbraCompileRequest request = {0};
    char outputPath[1024] = {0};
    UMBRA_ResultCode compileResult;

    request.inputPath = inputPath;
    request.outputDirectory = outputDirectory;
    request.entryPoint = "main";
    request.shaderModel = "6_5";
    request.vulkanVersion = "1.3";
    request.platformType = platformType;
    request.shaderType = shaderType;
    request.optimizationLevel = UMBRA_OPT_LEVEL_3;
    request.tRegShift = 0;
    request.sRegShift = 0;
    request.rRegShift = 0;
    request.uRegShift = 0;

    compileResult = UmbraCompiler_Compile(&request);
    printf("Compile (%s) %s -> %d\n", UMBRA_ShaderPlatformToString(platformType), inputPath, (int)compileResult);
    if (compileResult != UMBRA_RESULT_OK)
    {
        return 0;
    }

    if (!BuildOutputPath(outputPath, sizeof(outputPath), outputDirectory, inputPath, platformType))
    {
        printf("  Failed to build output path for reflection.\n");
        return 0;
    }

    return ReflectAndPrint(outputPath, shaderType, platformType);
}

int main(void)
{
    UmbraCompiler_SetLogCallback(OnCompilerLog, NULL);

    printf("UmbraCompiler version: %s\n", UmbraCompiler_GetVersion());

    const char* shadersDirectory = "Shaders";
    int compiledCount = 0;
    int failedCount = 0;

#ifdef _WIN32
    char pendingDirectories[256][MAX_PATH];
    int pendingCount = 0;
    snprintf(pendingDirectories[pendingCount++], MAX_PATH, "%s", shadersDirectory);

    while (pendingCount > 0)
    {
        char currentDirectory[MAX_PATH] = {0};
        snprintf(currentDirectory, MAX_PATH, "%s", pendingDirectories[--pendingCount]);

        char searchPattern[MAX_PATH] = {0};
        snprintf(searchPattern, sizeof(searchPattern), "%s\\*", currentDirectory);

        WIN32_FIND_DATAA findData;
        HANDLE findHandle = FindFirstFileA(searchPattern, &findData);
        if (findHandle == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        do
        {
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            {
                continue;
            }

            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                if (pendingCount < 256)
                {
                    snprintf(pendingDirectories[pendingCount++], MAX_PATH, "%s\\%s", currentDirectory, findData.cFileName);
                }
                continue;
            }

            if (!IsShaderSourceFile(findData.cFileName))
            {
                continue;
            }

            char inputPath[MAX_PATH] = {0};
            snprintf(inputPath, sizeof(inputPath), "%s/%s", currentDirectory, findData.cFileName);

            const char* outputDirectory = DetectOutputDirectory(inputPath);
            if (!EnsureDirectoryRecursive(outputDirectory))
            {
                printf("Failed to create output directory: %s\n", outputDirectory);
                failedCount++;
                continue;
            }

            {
                UMBRA_ShaderType shaderType = DetectShaderTypeFromFilename(findData.cFileName);
                if (CompileAndReflect(inputPath, outputDirectory, shaderType, UMBRA_SHADER_PLATFORM_TYPE_SPIRV))
                    compiledCount++;
                else
                    failedCount++;

                if (IsHlslFile(findData.cFileName))
                {
                    if (CompileAndReflect(inputPath, outputDirectory, shaderType, UMBRA_SHADER_PLATFORM_TYPE_DXIL))
                        compiledCount++;
                    else
                        failedCount++;
                }
            }
        } while (FindNextFileA(findHandle, &findData) != 0);

        FindClose(findHandle);
    }
#else
    char pendingDirectories[256][1024];
    int pendingCount = 0;
    snprintf(pendingDirectories[pendingCount++], sizeof(pendingDirectories[0]), "%s", shadersDirectory);

    while (pendingCount > 0)
    {
        char currentDirectory[1024] = {0};
        snprintf(currentDirectory, sizeof(currentDirectory), "%s", pendingDirectories[--pendingCount]);

        DIR* directory = opendir(currentDirectory);
        if (directory == NULL)
        {
            continue;
        }

        struct dirent* entry;
        while ((entry = readdir(directory)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            char fullPath[1024] = {0};
            snprintf(fullPath, sizeof(fullPath), "%s/%s", currentDirectory, entry->d_name);

            DIR* maybeDir = opendir(fullPath);
            if (maybeDir != NULL)
            {
                closedir(maybeDir);
                if (pendingCount < 256)
                {
                    snprintf(pendingDirectories[pendingCount++], sizeof(pendingDirectories[0]), "%s", fullPath);
                }
                continue;
            }

            if (!IsShaderSourceFile(entry->d_name))
            {
                continue;
            }

            const char* outputDirectory = DetectOutputDirectory(fullPath);
            if (!EnsureDirectoryRecursive(outputDirectory))
            {
                printf("Failed to create output directory: %s\n", outputDirectory);
                failedCount++;
                continue;
            }

            {
                UMBRA_ShaderType shaderType = DetectShaderTypeFromFilename(entry->d_name);
                if (CompileAndReflect(fullPath, outputDirectory, shaderType, UMBRA_SHADER_PLATFORM_TYPE_SPIRV))
                    compiledCount++;
                else
                    failedCount++;

                if (IsHlslFile(entry->d_name))
                {
                    if (CompileAndReflect(fullPath, outputDirectory, shaderType, UMBRA_SHADER_PLATFORM_TYPE_DXIL))
                        compiledCount++;
                    else
                        failedCount++;
                }
            }
        }

        closedir(directory);
    }
#endif

    printf("Compiled: %d, Failed: %d\n", compiledCount, failedCount);

    return (failedCount == 0 && compiledCount > 0) ? 0 : 1;
}
