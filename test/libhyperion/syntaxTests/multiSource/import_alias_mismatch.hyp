==== Source: s1.hyp ====
int constant a = 2;
==== Source: s2.hyp ====
import {a as e} from "s1.hyp";
import "s2.hyp" as M;
contract C {
  function f() public pure returns (int) { return M.a; }
}
// ----
// TypeError 9582: (s2.hyp:116-119): Member "a" not found or not visible after argument-dependent lookup in module "s2.hyp".
