==== Source: a.hyp ====
function f(uint) pure returns (uint) { return 7; }
function f(bytes memory x) pure returns (uint) { return x.length; }
==== Source: b.hyp ====
import "a.hyp" as M;
contract C {
    function f() public pure returns (uint, uint) {
        return (M.f(2), M.f("abc"));

    }
}
// ----
// f() -> 7, 3
