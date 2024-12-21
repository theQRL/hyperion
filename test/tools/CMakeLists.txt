add_subdirectory(ossfuzz)

add_subdirectory(yulInterpreter)
add_executable(yulrun yulrun.cpp)
target_link_libraries(yulrun PRIVATE yulInterpreter libhypc zvmasm Boost::boost Boost::program_options)

add_executable(hypfuzzer afl_fuzzer.cpp fuzzer_common.cpp)
target_link_libraries(hypfuzzer PRIVATE libhypc zvmasm Boost::boost Boost::program_options Boost::system)

add_executable(yulopti yulopti.cpp)
target_link_libraries(yulopti PRIVATE hyperion Boost::boost Boost::program_options Boost::system)

add_executable(ihyptest
	ihyptest.cpp
	IhypTestOptions.cpp
	../Common.cpp
	../CommonSyntaxTest.cpp
	../ZVMHost.cpp
	../TestCase.cpp
	../TestCaseReader.cpp
	../libhyperion/util/BytesUtils.cpp
	../libhyperion/util/Common.cpp
	../libhyperion/util/ContractABIUtils.cpp
	../libhyperion/util/TestFileParser.cpp
	../libhyperion/util/TestFunctionCall.cpp
	../libhyperion/GasTest.cpp
	../libhyperion/MemoryGuardTest.cpp
	../libhyperion/NatspecJSONTest.cpp
	../libhyperion/SyntaxTest.cpp
	../libhyperion/SemanticTest.cpp
	../libhyperion/AnalysisFramework.cpp
	../libhyperion/HyperionExecutionFramework.cpp
	../ExecutionFramework.cpp
	../libhyperion/ABIJsonTest.cpp
	../libhyperion/ASTJSONTest.cpp
	../libhyperion/ASTPropertyTest.cpp
	../libhyperion/SMTCheckerTest.cpp
	../libyul/Common.cpp
	../libyul/ControlFlowGraphTest.cpp
	../libyul/ControlFlowSideEffectsTest.cpp
	../libyul/ZVMCodeTransformTest.cpp
	../libyul/FunctionSideEffects.cpp
	../libyul/ObjectCompilerTest.cpp
	../libyul/SyntaxTest.cpp
	../libyul/StackShufflingTest.cpp
	../libyul/StackLayoutGeneratorTest.cpp
	../libyul/YulOptimizerTest.cpp
	../libyul/YulOptimizerTestCommon.cpp
	../libyul/YulInterpreterTest.cpp
)
target_compile_definitions(ihyptest PRIVATE IHYPTEST)
target_link_libraries(ihyptest PRIVATE zvmc libhypc hyperion yulInterpreter zvmasm Boost::boost Boost::program_options Boost::unit_test_framework Threads::Threads)
