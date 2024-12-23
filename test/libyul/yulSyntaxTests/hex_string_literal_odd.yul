{
    let name := hex"abc"
}
// ====
// dialect: zvm
// ----
// ParserError 1465: (18-24): Illegal token: Expected even number of hex-nibbles.
