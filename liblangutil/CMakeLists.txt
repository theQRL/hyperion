# Hyperion Commons Library (Hyperion related sharing bits between libhyperion and libyul)
set(sources
	Common.h
	CharStream.cpp
	CharStream.h
	DebugInfoSelection.cpp
	DebugInfoSelection.h
	ErrorReporter.cpp
	ErrorReporter.h
	ZVMVersion.h
	ZVMVersion.cpp
	Exceptions.cpp
	Exceptions.h
	ParserBase.cpp
	ParserBase.h
	Scanner.cpp
	Scanner.h
	CharStreamProvider.h
	SemVerHandler.cpp
	SemVerHandler.h
	SourceLocation.h
	SourceLocation.cpp
	SourceReferenceExtractor.cpp
	SourceReferenceExtractor.h
	SourceReferenceFormatter.cpp
	SourceReferenceFormatter.h
	Token.cpp
	Token.h
	UniqueErrorReporter.h
)

add_library(langutil ${sources})
target_link_libraries(langutil PUBLIC hyputil fmt::fmt-header-only)
