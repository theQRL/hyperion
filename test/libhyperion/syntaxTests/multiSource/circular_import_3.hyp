==== Source: s1.hyp ====
import {f as g, g as h} from "s2.hyp";
function f() pure returns (uint) { return h() - g(); }
==== Source: s2.hyp ====
import {f as h} from "s1.hyp";
function f() pure returns (uint) { return 2; }
function g() pure returns (uint) { return 4; }
==== Source: s3.hyp ====
import "s1.hyp";
import "s2.hyp";
// ----
// DeclarationError 1686: (s1.hyp:39-93): Function with same name and parameter types defined twice.
// DeclarationError 1686: (s2.hyp:31-77): Function with same name and parameter types defined twice.
// DeclarationError 1686: (s2.hyp:78-124): Function with same name and parameter types defined twice.
