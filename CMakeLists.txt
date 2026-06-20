cmake_minimum_required(VERSION 3.10)

# Nombre del proyecto
project(AetherCore VERSION 1.0 LANGUAGES CXX)

# Exigir C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Incluir la carpeta de cabeceras
include_directories(include)

# 1. Recopilar todos los archivos fuente
file(GLOB ALL_SOURCES "src/*.cpp")

# 2. Excluir el lanzador del binario principal de la base de datos
list(REMOVE_ITEM ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/aether_run.cpp")

# 3. Compilar el binario de la Base de Datos (AetherCore)
add_executable(aethercore ${ALL_SOURCES})
target_link_libraries(aethercore pthread)

# 4. Compilar el binario del Motor de Contenedores (AetherRun)
add_executable(aether_run src/aether_run.cpp)