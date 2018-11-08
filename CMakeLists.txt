cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

project(node-win32-np)

set(MODULE_NAME "node_win32_np")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${node-win32-np_SOURCE_DIR}/cmake")

include(NodeJS)

nodejs_init()

add_nodejs_module(
	${MODULE_NAME}
	src/module.cpp
	src/client.cpp src/client.hpp
	src/server.cpp src/server.hpp
	src/common.cpp src/common.hpp
	src/utility.hpp
)

target_include_directories(
	${MODULE_NAME}
	PRIVATE ${node-win32-np_SOURCE_DIR}/node_modules/node-addon-api
)

target_compile_definitions(
	${MODULE_NAME}
	PRIVATE
		-D_WIN32_WINNT=0x0601
		-DWINVER=0x0601
		-DWIN32_LEAN_AND_MEAN
		-DNOMINMAX
)

install(FILES $<TARGET_FILE:${MODULE_NAME}> DESTINATION .)
install(FILES $<TARGET_PDB_FILE:${MODULE_NAME}> DESTINATION . OPTIONAL)
install(FILES package.json main.js DESTINATION .)