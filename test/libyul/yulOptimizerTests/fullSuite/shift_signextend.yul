{
    let x := calldataload(0)
    let a := shl(sub(256, 8), signextend(0, x))
    sstore(0, a)
}
// ----
// step: fullSuite
//
// {
//     {
//         sstore(0, signextend(31, shl(248, calldataload(0))))
//     }
// }
