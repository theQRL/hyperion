{
    // A reentrancy lock.
    if tload(0) { revert(0, 0) }
    tstore(0, 1)
    pop(call(gas(), caller(), 0, 0, 0, 0, 0))
    sstore(0, tload(0))
    sstore(1, tload(1))
    tstore(0, 0)
}
// ----
// step: fullSuite
//
// {
//     {
//         if tload(0) { revert(0, 0) }
//         tstore(0, 1)
//         pop(call(gas(), caller(), 0, 0, 0, 0, 0))
//         sstore(0, tload(0))
//         sstore(1, tload(1))
//         tstore(0, 0)
//     }
// }
