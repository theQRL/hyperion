{
    let a := calldataload(0)
    let b := calldataload(64)
    tstore(a, b)
    // The value stored is not put in place of the load.
    let c := tload(a)
    sstore(a, c)
    // Writing transient storage leaves what is known of storage alone.
    tstore(a, 7)
    let d := sload(a)
    mstore(0, d)
}
// ----
// step: loadResolver
//
// {
//     {
//         let _1 := 0
//         let a := calldataload(_1)
//         tstore(a, calldataload(64))
//         let c := tload(a)
//         sstore(a, c)
//         tstore(a, 7)
//         mstore(_1, c)
//     }
// }
