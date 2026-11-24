{
    let b := 1
    for { let a := 1 } iszero(eq(a, 10)) { a := add(a, 1) } {
        // Not moved out of the loop: the copy in the loop writes memory.
        let inv := mload(b)
        mcopy(0x40, 0x80, 0x40)
        sstore(a, inv)
    }
}
// ----
// step: loopInvariantCodeMotion
//
// {
//     let b := 1
//     let a := 1
//     for { } iszero(eq(a, 10)) { a := add(a, 1) }
//     {
//         let inv := mload(b)
//         mcopy(0x40, 0x80, 0x40)
//         sstore(a, inv)
//     }
// }
