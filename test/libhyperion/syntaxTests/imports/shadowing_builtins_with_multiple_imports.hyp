==== Source: B.hyp ====
contract msg {} contract block{}
==== Source: b ====
import {msg, block} from "B.hyp";
contract C {
}
// ----
// Warning 2319: (B.hyp:0-15): This declaration shadows a builtin symbol.
// Warning 2319: (B.hyp:16-32): This declaration shadows a builtin symbol.
// Warning 2319: (b:8-11): This declaration shadows a builtin symbol.
// Warning 2319: (b:13-18): This declaration shadows a builtin symbol.
