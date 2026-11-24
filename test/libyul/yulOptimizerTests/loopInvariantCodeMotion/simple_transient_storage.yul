{
  let b := 1
  for { let a := 1 } iszero(eq(a, 10)) { a := add(a, 1) } {
    let inv := add(b, 42)
    let x := tload(mul(inv, 3))
    a := add(x, 1)
    mstore(a, inv)
    // does not invalidate transient storage
    sstore(a, inv)
  }

  // a static call cannot write transient storage
  for { let a := 1 } iszero(eq(a, 10)) { a := add(a, 1) } {
    let inv := add(b, 42)
    pop(staticcall(100, 0x010, 0x00, 32, 0x0100, 32))
    let x := tload(mul(inv, 3))
    a := add(x, 1)
  }
}
// ----
// step: loopInvariantCodeMotion
//
// {
//     let b := 1
//     let a := 1
//     let inv := add(b, 42)
//     let x := tload(mul(inv, 3))
//     for { } iszero(eq(a, 10)) { a := add(a, 1) }
//     {
//         a := add(x, 1)
//         mstore(a, inv)
//         sstore(a, inv)
//     }
//     let a_1 := 1
//     let inv_2 := add(b, 42)
//     let x_3 := tload(mul(inv_2, 3))
//     for { } iszero(eq(a_1, 10)) { a_1 := add(a_1, 1) }
//     {
//         pop(staticcall(100, 0x010, 0x00, 32, 0x0100, 32))
//         a_1 := add(x_3, 1)
//     }
// }
