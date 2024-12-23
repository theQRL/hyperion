==== Source: s1.hyp ====
function f() pure returns (uint16) { return 1337; }
function g() pure returns (uint8) { return 42; }
==== Source: s2.hyp ====
import {f as g} from "s1.hyp";
==== Source: s3.hyp ====
// imports f(uint16)->1337 as g(uint16)
import "s2.hyp";
// imports f(uint16)->1337 as f(uint16) and
// g(uint8)->42 as g(uint8)
import {f as f, g as g} from "s1.hyp";
contract C {
  function foo() public pure returns (uint) {
    // calls f()->1337 / f()->1337
    return f() / g();
  }
}
// ----
// DeclarationError 1686: (s1.hyp:0-51): Function with same name and parameter types defined twice.
