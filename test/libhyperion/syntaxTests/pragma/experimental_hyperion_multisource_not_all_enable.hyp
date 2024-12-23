==== Source: A.hyp ====
contract A {}
==== Source: B.hyp ====
pragma experimental hyperion;
import "A.hyp";
contract B {
    A a;
}
==== Source: C.hyp ====
pragma experimental hyperion;
import "A.hyp";
contract C {
    A a;
}
==== Source: D.hyp ====
pragma experimental hyperion;
import "A.hyp";
contract D {
    A a;
}
// ----
// ParserError 2141: (B.hyp:0-29): File declares "pragma experimental hyperion". If you want to enable the experimental mode, all source units must include the pragma.
// ParserError 2141: (C.hyp:0-29): File declares "pragma experimental hyperion". If you want to enable the experimental mode, all source units must include the pragma.
// ParserError 2141: (D.hyp:0-29): File declares "pragma experimental hyperion". If you want to enable the experimental mode, all source units must include the pragma.
