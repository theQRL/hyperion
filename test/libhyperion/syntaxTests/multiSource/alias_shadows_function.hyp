==== Source: s1.hyp ====
function f() pure returns (uint) { return 1337; }
function g() pure returns (uint) { return 42; }
==== Source: s2.hyp ====
import {f as g} from "s1.hyp";
==== Source: s3.hyp ====
// imports f()->1337 as g()
import "s2.hyp";
// imports f()->1337 as f() and
// g()->42 as g
import "s1.hyp";
contract C {
  function foo() public pure returns (uint) {
    // calls f()->1337 / f()->1337
    return f() / g();
  }
}
// ----
// DeclarationError 1686: (s1.hyp:0-49): Function with same name and parameter types defined twice.
