{
    function a() { tstore(0, 1) }
    function f() { a() }
    function g() { pop(tload(0)) }
    function h() { pop(staticcall(100, 0x010, 0x00, 32, 0x0100, 32)) }
    function i() { pop(sload(0)) tstore(0, 1) }
    function j() { pop(create(0, 0, 0)) }
}
// ----
// : movable, movable apart from effects, can be removed, can be removed if no msize
// a: writes transient storage
// f: writes transient storage
// g: movable apart from effects, can be removed, can be removed if no msize, reads transient storage
// h: writes other state, reads storage, writes memory, reads transient storage
// i: reads storage, writes transient storage
// j: writes other state, writes storage, reads memory, writes transient storage
