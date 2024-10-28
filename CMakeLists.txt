cmake_minimum_required(VERSION 3.6)

project(AntiBlock)

add_library(${PROJECT_NAME} SHARED
	src/main.cpp
)

hlcoop_setup_plugin("plugins/server/")