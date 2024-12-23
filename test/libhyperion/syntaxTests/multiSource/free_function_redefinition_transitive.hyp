==== Source: s1.hyp ====
function f() pure returns (uint) { return 1337; }
contract C {}
==== Source: s2.hyp ====
import "s1.hyp";
contract D is C {}
==== Source: s3.hyp ====
import "s2.hyp";
function f() pure returns (uint) { return 42; }
contract E is D {}
// ----
// DeclarationError 1686: (s3.hyp:17-64): Function with same name and parameter types defined twice.
