{
    let x := calldataload(0)
    let a := clz(x)
    // The value of `a`: the count depends on nothing but the argument.
    let b := clz(x)
    sstore(a, b)
}
// ----
// step: commonSubexpressionEliminator
//
// {
//     let x := calldataload(0)
//     let a := clz(x)
//     let b := a
//     sstore(a, a)
// }
