# development vehicle for pipico source library
ADD_DEFINITIONS("-DEXT_MHz=0")

# project include paths, separate lines for each git repo to make maintenance a smidgeon easier
INCLUDE_DIRECTORIES(".")
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/cortexm ${PROJECT_SOURCE_DIR}/cortexm/stm32)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/safely/cppext)

LIST(APPEND SOURCES
  #cortexm/core_cmfunc.cpp
  #cortexm/systick.cpp
  cortexm/nvic.cpp nvicTable.inc
  cortexm/cortexm0.s
  )

LIST(APPEND SOURCES
  RpiPico.cpp RpiPico.h
  #main for testing RpiPico:
  pipicodev.cpp
  )

