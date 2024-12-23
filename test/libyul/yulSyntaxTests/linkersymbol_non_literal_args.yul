{
    let library_name := "contract/library.hyp:L"
    let addr := linkersymbol(library_name)
}
// ====
// dialect: zvm
// ----
// TypeError 9114: (67-79): Function expects direct literals as arguments.
