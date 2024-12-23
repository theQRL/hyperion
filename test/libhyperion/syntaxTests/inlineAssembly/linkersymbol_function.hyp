contract C {
  function f() public pure {
    assembly {
      function linkersymbol(a) {}

      linkersymbol("contract/library.hyp:L")
    }
  }
}
// ----
// DeclarationError 5017: (63-90): The identifier "linkersymbol" is reserved and can not be used.
