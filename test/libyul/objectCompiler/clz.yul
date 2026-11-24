{
    sstore(0, clz(calldataload(0)))
}
// ----
// Assembly:
//     /* "source":33:34   */
//   0x00
//     /* "source":20:35   */
//   calldataload
//     /* "source":16:36   */
//   clz
//     /* "source":13:14   */
//   0x00
//     /* "source":6:37   */
//   sstore
//     /* "source":0:39   */
//   stop
// Bytecode: 5f351e5f5500
// Opcodes: PUSH0 CALLDATALOAD CLZ PUSH0 SSTORE STOP
// SourceMappings: 33:1:0:-:0;20:15;16:20;13:1;6:31;0:39
