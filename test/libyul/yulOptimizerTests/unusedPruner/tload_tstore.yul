{
    let a := tload(0)
    pop(tload(1))
    tstore(0, 1)
}
// ----
// step: unusedPruner
//
// { { tstore(0, 1) } }
