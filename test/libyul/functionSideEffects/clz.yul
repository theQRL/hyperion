{
    function a() -> x { x := clz(calldataload(0)) }
    function f() -> x { x := a() }
}
// ----
// : movable, movable apart from effects, can be removed, can be removed if no msize
// a: movable, movable apart from effects, can be removed, can be removed if no msize
// f: movable, movable apart from effects, can be removed, can be removed if no msize
