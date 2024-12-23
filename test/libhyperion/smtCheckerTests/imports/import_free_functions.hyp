==== Source: Address.hyp ====
pragma hyperion ^0.8.0;
function s() pure {}
==== Source: ERC20.hyp ====
pragma hyperion ^0.8.0;

import "./Address.hyp";

function sub(uint256 a, uint256 b) pure returns (uint256) {
    return a - b;
}

contract ERC20 {
    mapping (address => uint256) private _balances;

    function transferFrom(uint256 amount) public view {
        sub(_balances[msg.sender], amount);
    }
}
// ====
// SMTEngine: all
// ----
// Warning 3944: (ERC20.hyp:121-126): CHC: Underflow (resulting value less than 0) happens here.\nCounterexample:\n\namount = 1\n\nTransaction trace:\nERC20.constructor()\nERC20.transferFrom(1){ msg.sender: 0x52f6 }\n    ERC20.hyp:sub(0, 1) -- internal call
