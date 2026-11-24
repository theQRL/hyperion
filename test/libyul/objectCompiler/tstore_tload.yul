{
    tstore(0, 1)
    sstore(0, tload(0))
}
// ----
// Assembly:
//     /* "source":16:17   */
//   0x01
//     /* "source":13:14   */
//   0x00
//     /* "source":6:18   */
//   tstore
//     /* "source":39:40   */
//   0x00
//     /* "source":33:41   */
//   tload
//     /* "source":30:31   */
//   0x00
//     /* "source":23:42   */
//   sstore
//     /* "source":0:44   */
//   stop
// Bytecode: 60015f5d5f5c5f5500
// Opcodes: PUSH1 0x1 PUSH0 TSTORE PUSH0 TLOAD PUSH0 SSTORE STOP
// SourceMappings: 16:1:0:-:0;13;6:12;39:1;33:8;30:1;23:19;0:44
