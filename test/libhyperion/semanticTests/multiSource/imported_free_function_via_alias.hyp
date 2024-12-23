==== Source: s1.hyp ====
function f() pure returns (uint) { return 1337; }
contract C {
  function g() public pure virtual returns (uint) {
    return f();
  }
}
==== Source: s2.hyp ====
import "s1.hyp" as M;
function f() pure returns (uint) { return 6; }
contract D is M.C {
  function g() public pure override returns (uint) {
    return super.g() + f() * 10000;
  }
}
// ----
// g() -> 61337
