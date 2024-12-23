{
    setimmutable(0, loadimmutable("abc"), "abc")
}
// ====
// dialect: zvm
// ----
// TypeError 9114: (6-18): Function expects direct literals as arguments.
