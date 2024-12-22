contract C {
    function f() public view {
        assembly {
            sstore(0, 1)
            pop(create(0, 1, 2))
            pop(create2(0, 1, 2, 3))
            pop(call(0, 1, 2, 3, 4, 5, 6))
            pop(delegatecall(0, 1, 2, 3, 4, 5))
            log0(0, 1)
            log1(0, 1, 2)
            log2(0, 1, 2, 3)
            log3(0, 1, 2, 3, 4)
            log4(0, 1, 2, 3, 4, 5)

            // These two are disallowed too but the error suppresses other errors.
            //pop(msize())
            //pop(pc())
        }
    }
}
// ----
// TypeError 8961: (75-87): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (104-119): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (137-156): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (174-199): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (217-247): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (261-271): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (284-297): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (310-326): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (339-358): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (371-393): Function cannot be declared as view because this expression (potentially) modifies the state.
