cmake_minimum_required(VERSION 3.15)
# 工程名称client
project(client)

#添加显示定义，定义宏SIMULATOR_LINUX,这样才能在代码中识别到该宏
if(SIMULATOR_LINUX)
    add_compile_definitions(SIMULATOR_LINUX)
endif()
#拷贝res文件到build中，方便后续打包
file(COPY res DESTINATION ${PROJECT_BINARY_DIR})

# 编译当前目录下所有.c文件
aux_source_directory(. SOURCES)
aux_source_directory(./pages/ SOURCES)
aux_source_directory(./net/ SOURCES)

# 生成名称为app1的可执行文件
add_executable(client ${SOURCES})
# 引入头文件路径
target_include_directories(client PRIVATE
                                ./
                                ./res
                                ./pages
                                ./res/font
                                ./res/image
                                ${CMAKE_SOURCE_DIR}/lvgl
                                ${CMAKE_SOURCE_DIR}/lvgl/demos
                                ${CMAKE_SOURCE_DIR}/lvgl/src/extra/others/ime
                                ${CMAKE_SOURCE_DIR}/component/font
                                ${CMAKE_SOURCE_DIR}/platform/include/curl
                                )
# 引入依赖库
target_link_libraries(client PRIVATE
                        lvgl_demos
                        lvgl
                        font

                        curl
                        nghttp2
                        ssl
                        crypto
                        )
# 当使用 PRIVATE 关键字时，表示所链接的库仅用于当前目标的实现，而不会传递给依赖该目标的其他目标。
# 换句话说：
# 当前目标（demo2）会链接这些库并使用它们。
# 其他依赖 demo2的目标（比如另一个库或可执行文件）不会自动链接这些库。