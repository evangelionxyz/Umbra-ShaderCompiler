# Copyright (c) 2026 Evangelion Manuhutu

set(UMBRACOMPILER_C_EXAMPLE_TARGET UmbraCompilerCExample)

add_executable(${UMBRACOMPILER_C_EXAMPLE_TARGET}
    ${CMAKE_CURRENT_LIST_DIR}/C_Example.c
)

target_include_directories(${UMBRACOMPILER_C_EXAMPLE_TARGET} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../Source
)

target_link_libraries(${UMBRACOMPILER_C_EXAMPLE_TARGET} PRIVATE UmbraCompiler)

set_target_properties(${UMBRACOMPILER_C_EXAMPLE_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Example
)

if (MSVC)
    set_property(TARGET ${UMBRACOMPILER_C_EXAMPLE_TARGET} PROPERTY
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${UMBRACOMPILER_C_EXAMPLE_TARGET}>"
    )
endif()

add_custom_command(TARGET ${UMBRACOMPILER_C_EXAMPLE_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_LIST_DIR}/Shaders
        $<TARGET_FILE_DIR:${UMBRACOMPILER_C_EXAMPLE_TARGET}>/Shaders
    COMMENT "Copying shader examples for C example runtime"
)

if (WIN32)
    set(UMBRA_DXCOMPILER_RUNTIME_DLL "")
    if (EXISTS "$ENV{VULKAN_SDK}/Bin/dxcompiler.dll")
        set(UMBRA_DXCOMPILER_RUNTIME_DLL "$ENV{VULKAN_SDK}/Bin/dxcompiler.dll")
    elseif (EXISTS "$ENV{VULKAN_SDK}/Bin32/dxcompiler.dll")
        set(UMBRA_DXCOMPILER_RUNTIME_DLL "$ENV{VULKAN_SDK}/Bin32/dxcompiler.dll")
    endif()

    set(UMBRA_DXIL_RUNTIME_DLL "")
    if (EXISTS "$ENV{VULKAN_SDK}/Bin/dxil.dll")
        set(UMBRA_DXIL_RUNTIME_DLL "$ENV{VULKAN_SDK}/Bin/dxil.dll")
    elseif (EXISTS "$ENV{VULKAN_SDK}/Bin32/dxil.dll")
        set(UMBRA_DXIL_RUNTIME_DLL "$ENV{VULKAN_SDK}/Bin32/dxil.dll")
    endif()

    add_custom_command(TARGET ${UMBRACOMPILER_C_EXAMPLE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:UmbraCompiler>
            $<TARGET_FILE_DIR:${UMBRACOMPILER_C_EXAMPLE_TARGET}>/
        COMMENT "Copying UmbraCompiler runtime next to C example"
    )

    if (UMBRA_DXCOMPILER_RUNTIME_DLL)
        add_custom_command(TARGET ${UMBRACOMPILER_C_EXAMPLE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UMBRA_DXCOMPILER_RUNTIME_DLL}"
                $<TARGET_FILE_DIR:${UMBRACOMPILER_C_EXAMPLE_TARGET}>/
            COMMENT "Copying Vulkan SDK dxcompiler.dll next to C example"
        )
    else()
        message(WARNING "dxcompiler.dll not found under VULKAN_SDK/Bin or Bin32; runtime may use an incompatible DXC without SPIR-V codegen.")
    endif()

    if (UMBRA_DXIL_RUNTIME_DLL)
        add_custom_command(TARGET ${UMBRACOMPILER_C_EXAMPLE_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UMBRA_DXIL_RUNTIME_DLL}"
                $<TARGET_FILE_DIR:${UMBRACOMPILER_C_EXAMPLE_TARGET}>/
            COMMENT "Copying Vulkan SDK dxil.dll next to C example"
        )
    endif()
endif()
