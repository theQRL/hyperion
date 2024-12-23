==== Source: ExtCall.hyp ====
interface Unknown {
    function callme() external;
}

contract ExtCall {
    uint x;

    bool lock;
    modifier mutex {
        require(!lock);
        lock = true;
        _;
        lock = false;
    }

    function setX(uint y) mutex public {
        x = y;
    }

    function xMut(Unknown u) public {
        uint x_prev = x;
        u.callme();
        assert(x_prev == x);
    }
}
==== Source: ExtCall.t.hyp ====
import "ExtCall.hyp";

contract ExtCallTest {
    ExtCall call;

    function setUp() public {
        call = new ExtCall();
    }
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 6328: (ExtCall.hyp:362-381): CHC: Assertion violation happens here.
// Warning 8729: (ExtCall.t.hyp:110-123): Contract deployment is only supported in the trusted mode for external calls with the CHC engine.
