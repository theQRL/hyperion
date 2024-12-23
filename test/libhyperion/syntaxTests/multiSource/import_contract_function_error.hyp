==== Source: s1.hyp ====
contract C {
  function f() public pure returns (uint) {
    return 1337;
  }
}
==== Source: s2.hyp ====
import {C.f as g} from "s1.hyp";
// ----
// ParserError 2314: (s2.hyp:9-10): Expected '}' but got '.'
