{
    // The count is over the 512-bit word.
    sstore(0, clz(0))
    sstore(1, clz(1))
    sstore(2, clz(shl(255, 1)))
    sstore(3, clz(shl(256, 1)))
    sstore(4, clz(shl(511, 1)))
    sstore(5, clz(not(0)))
    // Not a constant.
    sstore(6, clz(calldataload(0)))
}
// ----
// step: expressionSimplifier
//
// {
//     {
//         let _1 := 0
//         sstore(_1, 512)
//         sstore(1, 511)
//         sstore(2, 256)
//         sstore(3, 255)
//         sstore(4, 0)
//         sstore(5, 0)
//         sstore(6, clz(calldataload(_1)))
//     }
// }
