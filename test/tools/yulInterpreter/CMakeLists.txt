set(sources
	QRVMInstructionInterpreter.h
	QRVMInstructionInterpreter.cpp
	Interpreter.h
	Interpreter.cpp
	Inspector.h
	Inspector.cpp
)

add_library(yulInterpreter ${sources})
target_link_libraries(yulInterpreter PUBLIC yul hyperion hyputil)
