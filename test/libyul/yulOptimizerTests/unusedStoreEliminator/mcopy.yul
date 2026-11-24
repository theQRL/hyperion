{
    let c := calldataload(0)

    // The copy reads this store, which keeps it.
    mstore(0x40, c)
    // The copy overwrites this store before anything reads it, which removes it.
    mstore(0x100, c)
    mcopy(0x100, 0x40, 0x40)

    // A copy is not removed, not even if nothing reads what it wrote.
    mcopy(0x200, 0x40, 0x40)

    return(0x100, 0x40)
}
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         mstore(0x40, calldataload(0))
//         let _3 := 0x100
//         mcopy(0x100, 0x40, 0x40)
//         mcopy(0x200, 0x40, 0x40)
//         return(0x100, 0x40)
//     }
// }
