contract C {
    constructor() {
        /// @hyperion memory-safe-assembly    a memory-safe-assembly
        assembly { mstore(0, 0) }
    }
    function f() internal pure {
        /// @hyperion a memory-safe-assembly
        assembly { mstore(0, 0) }
        /// @hyperion a
        ///           memory-safe-assembly
        ///           b
        assembly { mstore(0, 0) }
    }
}
// ----
// :C(creation) true
// :C(runtime) true
