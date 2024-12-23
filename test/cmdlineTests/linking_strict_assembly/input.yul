object "a" {
    code {
        let addr := linkersymbol("contract/test.hyp:L")
        sstore(0, addr)
    }
}
