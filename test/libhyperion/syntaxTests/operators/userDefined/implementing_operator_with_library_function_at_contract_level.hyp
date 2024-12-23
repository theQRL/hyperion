==== Source: external.hyp ====
type Int is int128;

library L {
    function binaryOperator(Int, Int) external pure returns (Int) {}
    function unaryOperator(Int) external pure returns (Int) {}

    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}

contract C {
    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}

library X {
    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}

==== Source: internal.hyp ====
type Int is int128;

library L {
    function binaryOperator(Int, Int) internal pure returns (Int) {}
    function unaryOperator(Int) internal pure returns (Int) {}

    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}

contract C {
    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}

library X {
    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}

==== Source: private.hyp ====
type Int is int128;

library L {
    function binaryOperator(Int, Int) private pure returns (Int) {}
    function unaryOperator(Int) private pure returns (Int) {}

    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}
==== Source: public.hyp ====
type Int is int128;

library L {
    function binaryOperator(Int, Int) public pure returns (Int) {}
    function unaryOperator(Int) public pure returns (Int) {}

    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}

contract C {
    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}

library X {
    using {L.binaryOperator as *} for Int;
    using {L.unaryOperator as ~} for Int;
}
// ----
// TypeError 3320: (external.hyp:177-193): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (external.hyp:177-193): Only pure free functions can be used to define operators.
// TypeError 3320: (external.hyp:220-235): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (external.hyp:220-235): Only pure free functions can be used to define operators.
// TypeError 3320: (external.hyp:278-294): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (external.hyp:278-294): Only pure free functions can be used to define operators.
// TypeError 3320: (external.hyp:321-336): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (external.hyp:321-336): Only pure free functions can be used to define operators.
// TypeError 3320: (external.hyp:378-394): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (external.hyp:378-394): Only pure free functions can be used to define operators.
// TypeError 3320: (external.hyp:421-436): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (external.hyp:421-436): Only pure free functions can be used to define operators.
// TypeError 3320: (internal.hyp:177-193): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (internal.hyp:177-193): Only pure free functions can be used to define operators.
// TypeError 3320: (internal.hyp:220-235): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (internal.hyp:220-235): Only pure free functions can be used to define operators.
// TypeError 3320: (internal.hyp:278-294): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (internal.hyp:278-294): Only pure free functions can be used to define operators.
// TypeError 3320: (internal.hyp:321-336): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (internal.hyp:321-336): Only pure free functions can be used to define operators.
// TypeError 3320: (internal.hyp:378-394): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (internal.hyp:378-394): Only pure free functions can be used to define operators.
// TypeError 3320: (internal.hyp:421-436): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (internal.hyp:421-436): Only pure free functions can be used to define operators.
// TypeError 3320: (private.hyp:175-191): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (private.hyp:175-191): Only pure free functions can be used to define operators.
// TypeError 3320: (private.hyp:218-233): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (private.hyp:218-233): Only pure free functions can be used to define operators.
// TypeError 3320: (public.hyp:173-189): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (public.hyp:173-189): Only pure free functions can be used to define operators.
// TypeError 3320: (public.hyp:216-231): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (public.hyp:216-231): Only pure free functions can be used to define operators.
// TypeError 3320: (public.hyp:274-290): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (public.hyp:274-290): Only pure free functions can be used to define operators.
// TypeError 3320: (public.hyp:317-332): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (public.hyp:317-332): Only pure free functions can be used to define operators.
// TypeError 3320: (public.hyp:374-390): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (public.hyp:374-390): Only pure free functions can be used to define operators.
// TypeError 3320: (public.hyp:417-432): Operators can only be defined in a global 'using for' directive.
// TypeError 7775: (public.hyp:417-432): Only pure free functions can be used to define operators.
