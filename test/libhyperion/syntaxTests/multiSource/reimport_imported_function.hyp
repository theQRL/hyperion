==== Source: s1.hyp ====
function f() pure returns (uint) { return 1337; }
==== Source: s2.hyp ====
import {f as g} from "s1.hyp";
==== Source: s3.hyp ====
import {g as h} from "s2.hyp";
contract C {
  function foo() public pure returns (uint) {
    return h();
  }
}
// ----
