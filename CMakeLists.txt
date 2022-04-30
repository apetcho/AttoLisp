cmake_minimum_required(VERSION 3.17)
project(AttoLisp LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(ATTOLISP_SRC src)
# set(ATTOLISP_BIN bin)
# set(CMAKE_BINARY_DIR ${ATTOLISP_BIN})
add_executable(AttoLisp 
    ${ATTOLISP_SRC}/attolisp.c ${ATTOLISP_SRC}/attolisp.h
)
