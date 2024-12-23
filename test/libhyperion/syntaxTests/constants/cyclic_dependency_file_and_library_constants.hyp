==== Source: A.hyp ====
import "B.hyp";

uint256 constant A = B.VAL + 1;

==== Source: B.hyp ====
import "A.hyp";

library B {
    uint256 constant VAL = A + 1;
}

// ----
// TypeError 6161: (B.hyp:33-61): The value of the constant VAL has a cyclic dependency via A.
// TypeError 6161: (A.hyp:17-47): The value of the constant A has a cyclic dependency via VAL.
