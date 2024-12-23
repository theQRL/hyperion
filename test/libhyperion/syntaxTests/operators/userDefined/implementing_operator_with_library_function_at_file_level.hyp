==== Source: external.hyp ====
type Int is int128;

library L {
    function binaryOperator(Int, Int) external pure returns (Int) {}
    function unaryOperator(Int) external pure returns (Int) {}
}

using {L.binaryOperator as +} for Int global;
using {L.unaryOperator as -} for Int global;

==== Source: internal.hyp ====
type Int is int128;

library L {
    function binaryOperator(Int, Int) internal pure returns (Int) {}
    function unaryOperator(Int) internal pure returns (Int) {}
}

using {L.binaryOperator as +} for Int global;
using {L.unaryOperator as -} for Int global;

==== Source: public.hyp ====
type Int is int128;

library L {
    function binaryOperator(Int, Int) public pure returns (Int) {}
    function unaryOperator(Int) public pure returns (Int) {}
}

using {L.binaryOperator as +} for Int global;
using {L.unaryOperator as -} for Int global;
// ----
// TypeError 7775: (external.hyp:175-191): Only pure free functions can be used to define operators.
// TypeError 7775: (external.hyp:221-236): Only pure free functions can be used to define operators.
// TypeError 7775: (internal.hyp:175-191): Only pure free functions can be used to define operators.
// TypeError 7775: (internal.hyp:221-236): Only pure free functions can be used to define operators.
// TypeError 7775: (public.hyp:171-187): Only pure free functions can be used to define operators.
// TypeError 7775: (public.hyp:217-232): Only pure free functions can be used to define operators.
