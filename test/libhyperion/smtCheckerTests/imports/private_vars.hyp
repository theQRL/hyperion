==== Source: ERC20.hyp ====
contract ERC20 {
    uint256 private a;
    function f() internal virtual {
        a = 2;
    }
}
==== Source: Token.hyp ====
import "ERC20.hyp";
contract Token is ERC20 {
    constructor() {
      f();
    }
}
// ====
// SMTEngine: all
// ----
