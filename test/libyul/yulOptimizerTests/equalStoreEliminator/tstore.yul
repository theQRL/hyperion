{
    let a := calldataload(0)
    let b := 20
    tstore(a, b)
    tstore(a, b)
    sstore(a, b)
    tstore(a, 7)
    sstore(a, b)
}
// ----
// step: equalStoreEliminator
//
// {
//     let a := calldataload(0)
//     let b := 20
//     tstore(a, b)
//     tstore(a, b)
//     sstore(a, b)
//     tstore(a, 7)
// }
