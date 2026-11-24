{
    // Reading transient storage does not make its dump appear: the optimizer may remove the tload.
    pop(tload(0))
    tstore(1, 0)
    sstore(0, tload(2))
}
// ----
// Trace:
// Memory dump:
// Storage dump:
