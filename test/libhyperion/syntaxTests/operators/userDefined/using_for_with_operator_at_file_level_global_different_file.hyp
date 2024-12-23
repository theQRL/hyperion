==== Source: s1.hyp ====
type Int is int;

==== Source: s2.hyp ====
import "s1.hyp";

function bitnot(Int) pure returns (Int) {}

using {bitnot as ~} for Int global;
// ----
// TypeError 4117: (s2.hyp:62-97): Can only use "global" with types defined in the same source unit at file level.
