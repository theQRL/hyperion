{
    function a() { mcopy(0x100, 0x200, 0x300) }
    function f() { a() }
    function g() { pop(mload(0)) mcopy(0, 0x40, 0x40) }
}
// ----
// : movable, movable apart from effects, can be removed, can be removed if no msize
// a: writes memory
// f: writes memory
// g: writes memory
