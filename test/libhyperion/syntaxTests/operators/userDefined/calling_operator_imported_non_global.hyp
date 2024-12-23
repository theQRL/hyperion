==== Source: s1.hyp ====
type Int is int;
using {add as +} for Int;
using {unsub as -} for Int;

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
// ----
// TypeError 3320: (s1.hyp:24-27): Operators can only be defined in a global 'using for' directive.
// TypeError 3320: (s1.hyp:50-55): Operators can only be defined in a global 'using for' directive.
// TypeError 2271: (s2.hyp:70-95): Built-in binary operator + cannot be applied to types Int and Int. No matching user-defined operator found.
// TypeError 4907: (s2.hyp:105-117): Built-in unary operator - cannot be applied to type Int. No matching user-defined operator found.
