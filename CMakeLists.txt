cmake_minimum_required(VERSION 3.21)
project(stredni_odborna_cinnost)

include(cmake/warnings.cmake)
include(cmake/static_analysis.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

find_package(daxa CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)

add_executable(${PROJECT_NAME} 
    "src/main.cpp"
    "src/application.cpp"
    "src/context.cpp"
    "src/pch.cpp"
    "src/graphics/window.cpp"
    "src/graphics/camera.cpp"
    "src/graphics/renderer.cpp"
    "src/graphics/texture.cpp"
    "src/graphics/model.cpp"
    "src/ecs/components.cpp"
    "src/ecs/entity.cpp"
    "src/ecs/scene.cpp"
    "src/ecs/uuid.cpp"
    "src/ui/ui.cpp"
    "src/ui/editor/scene_hiearchy_panel.cpp"
    "src/utils/file_io.cpp"
    "src/utils/gpu_metric.cpp"
)
target_precompile_headers(${PROJECT_NAME} PRIVATE "src/pch.hpp")

set_project_warnings(${PROJECT_NAME})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE daxa::daxa glfw imgui::imgui fastgltf::fastgltf glm::glm OpenEXR::OpenEXR implot::implot)
target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE "src")