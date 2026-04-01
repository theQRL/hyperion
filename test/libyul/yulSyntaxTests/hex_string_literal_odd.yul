{
    let name := hex"abc"
}
// ====
// dialect: qrvm
// ----
// ParserError 1465: (18-24): Illegal token: Expected even number of hex-nibbles.
