cmake_minimum_required(VERSION 3.10)

# Nombre del proyecto
project(AetherCore VERSION 1.0 LANGUAGES CXX)

# Exigir C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Incluir la carpeta de cabeceras
include_directories(include)

# Buscar todos los archivos fuente en src/
file(GLOB SOURCES "src/*.cpp")

# Crear el ejecutable
add_executable(aethercore ${SOURCES})

# Vincular la librería de threads (necesaria para std::shared_mutex y concurrencia futura)
find_package(Threads REQUIRED)
target_link_libraries(aethercore Threads::Threads)