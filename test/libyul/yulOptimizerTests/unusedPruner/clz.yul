{
    // Both are removed: the count has no side effect.
    let a := clz(calldataload(0))
    pop(clz(calldataload(1)))
}
// ----
// step: unusedPruner
//
// { { } }
