{
    let c := calldataload(0)
    mstore(0x40, c)
    // Any call that writes memory makes the step forget what it knows of memory, also this one.
    mcopy(0x40, 0x80, 0)
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
//         mcopy(0, 0, _1)
//         sstore(_1, mload(_2))
//     }
// }
