{
    // A copy of nothing uses neither of its two positions.
    mcopy(calldataload(1), calldataload(2), 0)
    mcopy(calldataload(3), calldataload(4), sub(42, 42))
    mcopy(calldataload(5), calldataload(6), 1)
}
// ----
// step: expressionSimplifier
//
// {
//     {
//         let _1 := 0
//         let _4 := 1
//         mcopy(0, 0, _1)
//         mcopy(0, 0, 0)
//         mcopy(calldataload(5), calldataload(6), _4)
//     }
// }
