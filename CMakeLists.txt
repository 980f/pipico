cmake_minimum_required(VERSION 3.17)
# name your project, sets ${PROJECT_NAME}
project(pipicodev)

# project language options, there will be defaults.
set(CMAKE_CXX_STANDARD 17)

#processor selection:
include(cortexm/rpi/rp2040.cmake)
#the above includes a shared file for compiler setup.

#project files are out of line in case we need to make l452.cmake and siblings include them so that the postamble will work better.
include(${PROJECT_SOURCE_DIR}/${PROJECT_NAME}.cmake)

#it would be nice if we could define the things herein sooner, but I haven't figured out how to forward reference the source list.
include(cortexm/postamble.cmake)
