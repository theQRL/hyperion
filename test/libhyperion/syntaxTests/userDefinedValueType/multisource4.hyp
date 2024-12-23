==== Source: s1.hyp ====
type MyInt is int;
==== Source: s2.hyp ====
import "s1.hyp" as M;
contract C {
  function f(int x) public pure returns (MyInt) { return MyInt.wrap(x); }
}
// ----
// DeclarationError 7920: (s2.hyp:76-81): Identifier not found or not unique.
