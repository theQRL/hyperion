contract C {
    function f() public pure {
        assembly {
            pop(sload(0))
            sstore(0, 1)
            pop(gas())
            pop(address())
            pop(balance(0))
            pop(caller())
            pop(callvalue())
            pop(extcodesize(0))
            extcodecopy(0, 1, 2, 3)
            pop(create(0, 1, 2))
            pop(call(0, 1, 2, 3, 4, 5, 6))
            pop(staticcall(0, 1, 2, 3, 4, 5))
            pop(delegatecall(0, 1, 2, 3, 4, 5))
            log0(0, 1)
            log1(0, 1, 2)
            log2(0, 1, 2, 3)
            log3(0, 1, 2, 3, 4)
            log4(0, 1, 2, 3, 4, 5)
            pop(origin())
            pop(gasprice())
            pop(blockhash(0))
            pop(coinbase())
            pop(timestamp())
            pop(number())
            pop(gaslimit())
            pop(extcodehash(0))
            pop(create2(0, 1, 2, 3))
            pop(selfbalance())
            pop(chainid())
            pop(basefee())
            pop(prevrandao())

            // These two are disallowed too but the error suppresses other errors.
            //pop(msize())
            //pop(pc())
        }
    }
}
// ----
// TypeError 2527: (79-87): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 8961: (101-113): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 2527: (130-135): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (153-162): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (180-190): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (208-216): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (234-245): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (263-277): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (291-314): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 8961: (331-346): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 8961: (364-389): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 2527: (407-435): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 8961: (453-483): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 8961: (497-507): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 8961: (520-533): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 8961: (546-562): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 8961: (575-594): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 8961: (607-629): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 2527: (646-654): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (672-682): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (700-712): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (730-740): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (758-769): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (787-795): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (813-823): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (841-855): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 8961: (873-892): Function cannot be declared as pure because this expression (potentially) modifies the state.
// TypeError 2527: (910-923): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (941-950): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (968-977): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
// TypeError 2527: (995-1007): Function declared as pure, but this expression (potentially) reads from the environment or state and thus requires "view".
