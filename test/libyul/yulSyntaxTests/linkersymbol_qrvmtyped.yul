{
    let addr:u256 := linkersymbol("contract/library.hyp:L")
    function linkersymbol(x) {}
}
// ====
// dialect: qrvmTyped 
// ----
// ParserError 5568: (75-87): Cannot use builtin function name "linkersymbol" as identifier name.
