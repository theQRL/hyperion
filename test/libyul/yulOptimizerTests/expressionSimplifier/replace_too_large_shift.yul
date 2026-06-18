{
    let a := shl(299, calldataload(0))
    let b := shr(299, calldataload(1))
    let c := shl(255, calldataload(2))
    let d := shr(255, calldataload(3))
    sstore(a, b)
    sstore(c, d)
}
// ----
// step: expressionSimplifier
//
// {
//     {
//         let _2 := calldataload(0)
//         let _3 := 299
//         let a := shl(_3, _2)
//         let b := shr(_3, calldataload(1))
//         let _8 := calldataload(2)
//         let _9 := 255
//         let c := shl(_9, _8)
//         let d := shr(_9, calldataload(3))
//         sstore(a, b)
//         sstore(c, d)
//     }
// }
