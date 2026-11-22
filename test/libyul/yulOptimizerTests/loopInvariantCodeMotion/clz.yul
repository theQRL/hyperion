{
    let x := calldataload(0)
    for { let i := 0 } lt(i, 4) { i := add(i, 1) } {
        // Moved in front of the loop: the count depends on nothing but the argument.
        let n := clz(x)
        sstore(i, n)
    }
}
// ----
// step: loopInvariantCodeMotion
//
// {
//     let x := calldataload(0)
//     let i := 0
//     let n := clz(x)
//     for { } lt(i, 4) { i := add(i, 1) }
//     { sstore(i, n) }
// }
