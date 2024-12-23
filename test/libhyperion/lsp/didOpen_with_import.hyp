// SPDX-License-Identifier: UNLICENSED
pragma hyperion >=0.8.0;

import './goto/lib.hyp';

contract C
{
    function f(uint a, uint b) public pure returns (uint)
    {
        return Lib.add(2 * a, b);
        //     ^^^^^^^ @diagnostics
    }
}
