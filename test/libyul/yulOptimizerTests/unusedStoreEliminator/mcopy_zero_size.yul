{
    let c := calldataload(0)

    // A copy of nothing neither reads nor overwrites a store.
    mstore(0x40, c)
    mcopy(0x40, 0x40, 0)
    mstore(0x40, 1)

    return(0x40, 0x40)
}
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         let c := calldataload(0)
//         let _2 := 0x40
//         mcopy(0x40, 0x40, 0)
//         mstore(0x40, 1)
//         return(0x40, 0x40)
//     }
// }
