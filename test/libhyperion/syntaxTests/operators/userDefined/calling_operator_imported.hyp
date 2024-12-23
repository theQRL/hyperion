==== Source: s1.hyp ====
type Int is int;
using {add as +} for Int global;
using {unsub as -} for Int global;

function add(Int, Int) pure returns (Int) {}
function unsub(Int) pure returns (Int) {}

==== Source: s2.hyp ====
import "s1.hyp";

contract C {
    function f() pure public {
        Int.wrap(0) + Int.wrap(0);
        -Int.wrap(0);
    }
}
