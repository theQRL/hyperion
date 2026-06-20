// Test where the location of the memory offset is not known at compile time
// >>> import web3
// >>> asBytes = int(500).to_bytes(64, byteorder='big')
// >>> int.from_bytes(web3.Web3.keccak(asBytes[:32]), byteorder='big')
// 18569430475105882587588266137607568536673111973893317399460219858819262702947
// >>> int.from_bytes(web3.Web3.keccak(asBytes[:16]), byteorder='big')
// 110620294328144418057589324861608220015688365608948720310623173341503153578932
{
    let offset := calldataload(0)
    mstore(offset, 500)
    sstore(0, keccak256(offset, 32))
    sstore(1, keccak256(offset, 16))
}
// ----
// step: loadResolver
//
// {
//     {
//         let _1 := 0
//         mstore(calldataload(_1), 500)
//         sstore(_1, 18569430475105882587588266137607568536673111973893317399460219858819262702947)
//         sstore(1, 110620294328144418057589324861608220015688365608948720310623173341503153578932)
//     }
// }
