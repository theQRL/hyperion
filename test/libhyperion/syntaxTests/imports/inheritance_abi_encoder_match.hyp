==== Source: A.hyp ====
pragma abicoder               v2;

contract A
{
    struct S { uint a; }
    S public s;
    function f(S memory _s) public returns (S memory,S memory) { }
}
==== Source: B.hyp ====
pragma abicoder               v2;

import "./A.hyp";
contract B is A { }
==== Source: C.hyp ====
pragma abicoder               v2;

import "./B.hyp";
contract C is B { }
// ----
