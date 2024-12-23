==== Source: definition.hyp ====
import "type-and-binding.hyp";

function add(Int, Int) pure returns (Int) {}
function unsub(Int) pure returns (Int) {}

==== Source: type-and-binding.hyp ====
import "definition.hyp";

type Int is int;

using {add as +} for Int global;
using {unsub as -} for Int global;

==== Source: use.hyp ====
import "type-and-binding.hyp";

contract C {
    function f() pure public {
        Int.wrap(0) + Int.wrap(0);
        -Int.wrap(0);
    }
}
