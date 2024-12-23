==== Source: s1.hyp ====
function f() pure returns (uint) { return 1337; }
==== Source: s2.hyp ====
import {f as f, f as f} from "s1.hyp";
contract C {
  function g() public pure returns (uint) {
    return f();
  }
}
// ----
