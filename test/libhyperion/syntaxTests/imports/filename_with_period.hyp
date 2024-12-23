==== Source: a/.b.hyp ====
contract B {}
==== Source: a/a.hyp ====
import ".b.hyp"; contract A is B {}
// ----
// ParserError 6275: (a/a.hyp:0-16): Source ".b.hyp" not found: File not supplied initially.
