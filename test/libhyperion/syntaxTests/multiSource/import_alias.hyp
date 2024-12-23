==== Source: s1.hyp ====
int constant a = 2;
==== Source: s2.hyp ====
import {a as e} from "s1.hyp";
import "s2.hyp" as M;
contract C {
  function f() public pure returns (int) { return M.e; }
}
// ----
