{
    function a() { sstore(0, 1) }
    function f() { a() }
    function g() { pop(delegatecall(0x010, 10, 0x00, 32, 0x0100, 32))}
    function h() { pop(sload(0))}
}
// ----
// : movable, movable apart from effects, can be removed, can be removed if no msize
// a: writes storage
// f: writes storage
// g: writes other state, writes storage, writes memory, writes transient storage
// h: movable apart from effects, can be removed, can be removed if no msize, reads storage
