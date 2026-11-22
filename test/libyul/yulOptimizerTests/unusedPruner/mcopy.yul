{
    // Has no return value, but it is not removed: it writes memory.
    mcopy(0x100, 0x200, 0x300)
}
// ----
// step: unusedPruner
//
// {
//     { mcopy(0x100, 0x200, 0x300) }
// }
