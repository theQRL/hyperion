{
    let c := calldataload(0)
    mstore(0x40, c)
    // Not resolved after the copy: it may have overwritten the word.
    mcopy(calldataload(0x40), 0x80, 0x40)
    sstore(0, mload(0x40))
}
// ----
// step: loadResolver
//
// {
//     {
//         let _1 := 0
//         let c := calldataload(_1)
//         let _2 := 0x40
//         mstore(_2, c)
//         mcopy(calldataload(_2), 0x80, _2)
//         sstore(_1, mload(_2))
//     }
// }
