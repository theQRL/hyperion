{
    let c := calldataload(0)

    // The copy overwrites only a part of this store, which keeps it.
    mstore(0x100, c)
    mcopy(0x100, 0x200, 0x20)

    // The copy has nothing to do with this store, which is removed because nothing reads it.
    mstore(0x300, c)
    mcopy(0x400, 0x200, 0x40)

    return(0x100, 0x40)
}
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         mstore(0x100, calldataload(0))
//         mcopy(0x100, 0x200, 0x20)
//         let _6 := 0x300
//         mcopy(0x400, 0x200, 0x40)
//         return(0x100, 0x40)
//     }
// }
