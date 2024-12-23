==== Source: binding.hyp ====
import "definition.hyp";
import "type.hyp";

using {add as +} for Int global;
using {unsub as -} for Int global;

==== Source: definition.hyp ====
import "type.hyp";

function add(Int, Int) pure returns (Int) {}
function unsub(Int) pure returns (Int) {}

==== Source: type.hyp ====
type Int is int;

==== Source: use.hyp ====
import "type.hyp";

contract C {
    function f() pure public {
        Int.wrap(0) + Int.wrap(0);
        -Int.wrap(0);
    }
}
// ----
// TypeError 4117: (binding.hyp:45-77): Can only use "global" with types defined in the same source unit at file level.
// TypeError 4117: (binding.hyp:78-112): Can only use "global" with types defined in the same source unit at file level.
// TypeError 2271: (use.hyp:72-97): Built-in binary operator + cannot be applied to types Int and Int. No matching user-defined operator found.
// TypeError 4907: (use.hyp:107-119): Built-in unary operator - cannot be applied to type Int. No matching user-defined operator found.
