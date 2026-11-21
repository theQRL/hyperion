{
    let a := tload(0)
    let b := tload(0)
    tstore(0, 1)
    let c := tload(0)
    mstore(a, add(b, c))
}
// ----
// step: commonSubexpressionEliminator
//
// {
//     let a := tload(0)
//     let b := tload(0)
//     tstore(0, 1)
//     let c := tload(0)
//     mstore(a, add(b, c))
// }
