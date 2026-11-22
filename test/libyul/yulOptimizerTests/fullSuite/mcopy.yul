{
    let c := calldataload(0)
    mstore(0x40, c)
    mstore(0x100, not(c))
    mcopy(0x100, 0x40, 0x40)
    mcopy(0x200, 0x100, 0x40)
    sstore(0, mload(0x100))
    return(0x200, 0x40)
}
// ----
// step: fullSuite
//
// {
//     {
//         mstore(0x40, calldataload(0))
//         let _1 := 0x100
//         mcopy(_1, 0x40, 0x40)
//         let _2 := 0x200
//         mcopy(_2, _1, 0x40)
//         sstore(0, mload(_1))
//         return(_2, 0x40)
//     }
// }
