==== Source: A.hyp ====
import "B.hyp";

uint256 constant A = B + 1;

==== Source: B.hyp ====
import "A.hyp";

uint256 constant B = A + 1;

// ----
// TypeError 6161: (B.hyp:17-43): The value of the constant B has a cyclic dependency via A.
// TypeError 6161: (A.hyp:17-43): The value of the constant A has a cyclic dependency via B.
