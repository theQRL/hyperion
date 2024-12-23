object "a" {
    code {
        let addr1 := linkersymbol("library1.hyp:L")
        let addr2 := linkersymbol("library2.hyp:L")
        sstore(0, addr1)
        sstore(1, addr2)
    }
}
