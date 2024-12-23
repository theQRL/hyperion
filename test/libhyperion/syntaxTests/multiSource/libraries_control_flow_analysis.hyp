==== Source: s1.hyp ====
library L
{
	function normal() public pure returns (uint) { return 1337; }
	function reverting() public pure returns (uint) { revert(); }
}
==== Source: s2.hyp ====
import "s1.hyp";
contract C
{
	function foo() public pure returns (uint) { L.normal(); }
	function bar() public pure returns (uint) { L.reverting(); }
}
// ----
// Warning 6321: (s2.hyp:67-71): Unnamed return variable can remain unassigned. Add an explicit return with value to all non-reverting code paths or name the variable.
// Warning 6321: (s2.hyp:126-130): Unnamed return variable can remain unassigned. Add an explicit return with value to all non-reverting code paths or name the variable.
