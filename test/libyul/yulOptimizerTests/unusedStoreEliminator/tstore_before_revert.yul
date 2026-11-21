{
    let c := calldataload(0)
    sstore(c, 1)
    tstore(c, 1)
    revert(0, 0)
}
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         let c := calldataload(0)
//         let _2 := 1
//         tstore(c, 1)
//         revert(0, 0)
//     }
// }
