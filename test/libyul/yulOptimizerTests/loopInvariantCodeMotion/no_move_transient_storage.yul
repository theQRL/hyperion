{
  let b := 1
  // invalidates transient storage in post
  for { let a := 1 } iszero(eq(a, 10)) { tstore(0x00, 0x01)} {
    let inv := add(b, 42)
    let x := tload(mul(inv, 3))
    a := add(x, 1)
    mstore(a, inv)
  }

  // invalidates transient storage in body
  for { let a := 1 } iszero(eq(a, 10)) { a := add(a, 1) } {
    let inv := add(b, 42)
    let x := tload(mul(inv, 3))
    a := add(x, 1)
    tstore(a, inv)
  }

  // invalidates state in body
  for { let a := 1 } iszero(eq(a, 10)) { a := add(a, 1) } {
    let inv := add(b, 42)
    pop(call(100, 0x010, 0, 0x00, 32, 0x0100, 32))
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
//     for { } iszero(eq(a, 10)) { tstore(0x00, 0x01) }
//     {
//         let x := tload(mul(inv, 3))
//         a := add(x, 1)
//         mstore(a, inv)
//     }
//     let a_1 := 1
//     let inv_2 := add(b, 42)
//     for { } iszero(eq(a_1, 10)) { a_1 := add(a_1, 1) }
//     {
//         let x_3 := tload(mul(inv_2, 3))
//         a_1 := add(x_3, 1)
//         tstore(a_1, inv_2)
//     }
//     let a_4 := 1
//     let inv_5 := add(b, 42)
//     for { } iszero(eq(a_4, 10)) { a_4 := add(a_4, 1) }
//     {
//         pop(call(100, 0x010, 0, 0x00, 32, 0x0100, 32))
//         let x_6 := tload(mul(inv_5, 3))
//         a_4 := add(x_6, 1)
//     }
// }
