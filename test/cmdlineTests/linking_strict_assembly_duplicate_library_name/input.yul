object "a" {
    code {
        let addr := linkersymbol("library.hyp:L")
        sstore(0, addr)
    }
}
