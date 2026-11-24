{
  let b := 1
  // a write to transient storage does not invalidate storage
  for { let a := 1 } iszero(eq(a, 10)) { a := add(a, 1) } {
    let inv := add(b, 42)
    let x := sload(mul(inv, 3))
    a := add(x, 1)
    tstore(a, inv)
  }
}
// ----
// step: loopInvariantCodeMotion
//
// {
//     let b := 1
//     let a := 1
//     let inv := add(b, 42)
//     let x := sload(mul(inv, 3))
//     for { } iszero(eq(a, 10)) { a := add(a, 1) }
//     {
//         a := add(x, 1)
//         tstore(a, inv)
//     }
// }
