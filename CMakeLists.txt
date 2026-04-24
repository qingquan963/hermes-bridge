cmake_minimum_required(VERSION 3.15)
project(HermesBridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: static CRT /MT (not /MD)
string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
string(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

# MSVC extra flags
add_compile_options(/W4 /WX-)
add_compile_options(/bigobj)

# Include directories: project include + src
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

add_executable(hermes_bridge
    src/main.cpp
    src/Config.cpp
    src/ThreadPool.cpp
    src/CommandQueue.cpp
    src/FileMonitor.cpp
    src/ResultWriter.cpp
    src/StateFile.cpp
    src/Logger.cpp
    src/CallbackClient.cpp
    src/HttpServer.cpp
    src/CallbackWriter.cpp
    src/handlers/IHandler.cpp
    src/handlers/ExecHandler.cpp
    src/handlers/FileHandler.cpp
    src/handlers/HttpHandler.cpp
    src/handlers/OllamaHandler.cpp
    src/handlers/ProcessHandler.cpp
    src/handlers/ServiceHandler.cpp
    src/handlers/TcpProxyHandler.cpp
)

# Link Windows native libraries
target_link_libraries(hermes_bridge PRIVATE
    winhttp.lib
    ws2_32.lib
)

# Output to project root
set_target_properties(hermes_bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
