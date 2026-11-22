{
    // The position of the highest set bit of a value known at compile time and of one that is not.
    sstore(0, sub(511, clz(0x1234)))
    sstore(1, sub(511, clz(calldataload(0))))
}
// ----
// step: fullSuite
//
// {
//     {
//         sstore(0, 12)
//         sstore(1, sub(511, clz(calldataload(0))))
//     }
// }
