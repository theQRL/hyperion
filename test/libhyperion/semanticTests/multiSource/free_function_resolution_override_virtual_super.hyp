==== Source: s1.hyp ====
function f() pure returns (uint) { return 1337; }
contract C {
  function g() public pure virtual returns (uint) {
    return f();
  }
}
==== Source: s2.hyp ====
import "s1.hyp";
contract D is C {
  function g() public pure override returns (uint) {
    return super.g();
  }
}
// ----
// g() -> 1337
