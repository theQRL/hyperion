==== Source: s1.hyp ====
function f() pure returns (uint) { return 1337; }
contract C {
  function g() public pure returns (uint) {
    return f();
  }
}
==== Source: s2.hyp ====
import "s1.hyp";
contract D is C {
  function h() public pure returns (uint) {
    return g();
  }
}
// ----
// h() -> 1337
