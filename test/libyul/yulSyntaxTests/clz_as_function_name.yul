{
    function clz() {}
    clz()
}
// ====
// dialect: qrvm
// ----
// ParserError 5568: (15-18): Cannot use builtin function name "clz" as identifier name.
