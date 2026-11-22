{
    let a := mload(0x40)
    mcopy(0x40, 0x80, 0x40)
    // Not the value of `a`: the copy is in between.
    let b := mload(0x40)
    sstore(a, b)
}
// ----
// step: commonSubexpressionEliminator
//
// {
//     let a := mload(0x40)
//     mcopy(0x40, 0x80, 0x40)
//     let b := mload(0x40)
//     sstore(a, b)
// }
