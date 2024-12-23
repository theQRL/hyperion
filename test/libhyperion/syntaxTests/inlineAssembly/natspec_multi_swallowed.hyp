function f() pure {
    /// @unrelated bogus-value

    /// @before
    ///
    /// @hyperion a   memory-safe-assembly b    c
    ///           d
    /// @after bogus-value
    assembly {}
    /// @hyperion memory-safe-assembly a a a
    ///           memory-safe-assembly
    assembly {}
}
// ----
// Warning 6269: (177-188): Unexpected NatSpec tag "after" with value "bogus-value" in inline assembly.
// Warning 6269: (177-188): Unexpected NatSpec tag "before" with value "@hyperion a   memory-safe-assembly b    c           d" in inline assembly.
// Warning 8787: (277-288): Unexpected value for @hyperion tag in inline assembly: a
// Warning 4377: (277-288): Value for @hyperion tag in inline assembly specified multiple times: a
// Warning 4377: (277-288): Value for @hyperion tag in inline assembly specified multiple times: memory-safe-assembly
