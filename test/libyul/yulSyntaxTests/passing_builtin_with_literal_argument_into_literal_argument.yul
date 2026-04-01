{
    setimmutable(0, loadimmutable("abc"), "abc")
}
// ====
// dialect: qrvm
// ----
// TypeError 9114: (6-18): Function expects direct literals as arguments.
