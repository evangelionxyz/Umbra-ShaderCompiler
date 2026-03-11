# Umbra ShaderCompiler

Umbra ShaderCompiler is a shared-library shader toolchain that compiles HLSL/GLSL and provides reflection data for runtime pipeline setup.

## What this project does
- Builds a shared library named `UmbraCompiler`.
- Compiles **HLSL** using DXC.
- Compiles **GLSL** to **SPIR-V** using shaderc.
- Supports output targets:
  - `SPIRV` (`.spirv`)
  - `DXIL` (`.dxil`, HLSL path)
  - `DXBC` (enum and extension support in core types)
- Provides reflection for:
  - `SPIR-V` (via SPIRV-Cross C API)
  - `DXIL` (via DirectX shader reflection APIs)
- Exposes both:
  - C++ API (`ShaderCompiler.h`)
  - C API (`ShaderCompilerCAPI.h`)

## Requirements
- CMake 3.20+
- C++20 compiler
- Vulkan SDK (`VULKAN_SDK` set)
- On Windows:
  - DXC runtime/library (`dxcompiler`)
  - DirectX libraries used by CMake (`d3d12`, `dxgi`, `d3dcompiler`, `dxguid`)

## Build
From the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

## Library APIs

### C++ API
Primary header: `Source/ShaderCompiler.h`

Main entry points:
- `umbra::ShaderCompiler::CompileDXC(...)`
- `umbra::ShaderCompiler::CompileGLSL(...)`
- `umbra::ShaderReflection::SPIRVReflect(...)`
- `umbra::ShaderReflection::DXILReflect(...)`

### C API
Primary header: `Source/ShaderCompilerCAPI.h`

Main entry points:
- `UmbraCompiler_Compile(...)`
- `UmbraCompiler_ReflectSPIRV(...)`
- `UmbraCompiler_ReflectDXIL(...)`
- `UmbraCompiler_FreeReflectionInfo(...)`

## Logging
Both APIs support callback-based logging with typed levels:
- `INFO`
- `WARNING`
- `ERROR`

Use these callbacks to route compiler/reflection diagnostics into your engine logs.

## Example programs
- C example: compiles shader files recursively, emits SPIR-V and DXIL (for HLSL), and prints reflection summary.
- C++ example: same flow using the native C++ API.

Both examples copy shader assets to runtime output and can be enabled with `UMBRACOMPILER_BUILD_EXAMPLES=ON`.

## Typical workflow
1. Fill compile options/request (entry point, shader model, platform target, optimization).
2. Compile to binary output.
3. Read produced bytecode and run reflection.
4. Consume reflection data to build resource layouts, stage IO, and vertex input descriptions.

## Notes
- `SPIR-V` reflection input must be valid SPIR-V bytecode.
- `DXIL` reflection path is platform-dependent (Windows DirectX tooling).
- For C API reflection results, always call `UmbraCompiler_FreeReflectionInfo` after use.
