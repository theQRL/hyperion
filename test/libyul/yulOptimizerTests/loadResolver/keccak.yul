{
    mstore(0, 10)
    /// >>> word = int.to_bytes(10, 64, byteorder='big')
    /// >>> int.from_bytes(web3.Web3.keccak(word[:32]), byteorder='big')
    /// 18569430475105882587588266137607568536673111973893317399460219858819262702947
    let val := keccak256(0, 32)
    sstore(0, val)
}
// ----
// step: loadResolver
//
// {
//     {
//         let _1 := 10
//         let _2 := 0
//         mstore(_2, _1)
//         sstore(_2, 18569430475105882587588266137607568536673111973893317399460219858819262702947)
//     }
// }
