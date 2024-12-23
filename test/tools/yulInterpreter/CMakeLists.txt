set(sources
	ZVMInstructionInterpreter.h
	ZVMInstructionInterpreter.cpp
	Interpreter.h
	Interpreter.cpp
	Inspector.h
	Inspector.cpp
)

add_library(yulInterpreter ${sources})
target_link_libraries(yulInterpreter PUBLIC yul hyperion hyputil)
