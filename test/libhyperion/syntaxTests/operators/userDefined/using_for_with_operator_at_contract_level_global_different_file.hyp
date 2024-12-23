==== Source: s1.hyp ====
type Int is int;

==== Source: s2.hyp ====
import "s1.hyp";

function bitnot(Int) pure returns (Int) {}

contract C {
    using {bitnot as ~} for Int global;
}
// ----
// SyntaxError 3367: (s2.hyp:79-114): "global" can only be used at file level.
