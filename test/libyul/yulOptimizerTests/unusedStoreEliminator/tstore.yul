{
    let c := calldataload(0)
    // Writes to transient storage are never removed, and reads of it
    // do not keep a write to storage alive.
    tstore(c, 1)
    tstore(c, 2)
    sstore(c, 1)
    pop(tload(c))
    sstore(c, 2)
}
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         let c := calldataload(0)
//         tstore(c, 1)
//         tstore(c, 2)
//         let _4 := 1
//         pop(tload(c))
//         sstore(c, 2)
//     }
// }
