{
    mcopy(0x100, 0x200, 0x300)
}
// ----
// Assembly:
//     /* "source":26:31   */
//   0x0300
//     /* "source":19:24   */
//   0x0200
//     /* "source":12:17   */
//   0x0100
//     /* "source":6:32   */
//   mcopy
//     /* "source":0:34   */
//   stop
// Bytecode: 6103006102006101005e00
// Opcodes: PUSH2 0x300 PUSH2 0x200 PUSH2 0x100 MCOPY STOP
// SourceMappings: 26:5:0:-:0;19;12;6:26;0:34
