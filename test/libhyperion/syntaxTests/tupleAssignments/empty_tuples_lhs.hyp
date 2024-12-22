contract C {
    function f0() public { (()) = 2; }

    function f1() public pure { (()) = (); }

    //#8711
    function f2() internal pure returns (uint, uint) { return () = f2(); }

    //#8277
    function f3()public{return()=();}

    //#8277
    function f4 ( bytes memory pubkey , bytes memory withdrawal_credentials , bytes memory amount , bytes memory signature, uint block_expired , bytes32 salt ) public returns ( address ) {
        require ( ( ( ) ) |= keccak256 ( abi . encodePacked ( block_expired , salt ) ) ) ;        
        return depositroot ( pubkey , withdrawal_credentials , amount , signature ) ;
    }
}
// ----
// TypeError 5547: (41-43): Empty tuple on the left hand side.
// TypeError 7407: (47-48): Type int_const 2 is not implicitly convertible to expected type tuple().
// TypeError 5547: (86-88): Empty tuple on the left hand side.
// TypeError 5547: (173-175): Empty tuple on the left hand side.
// TypeError 7407: (178-182): Type tuple(uint256,uint256) is not implicitly convertible to expected type tuple().
// TypeError 5132: (166-182): Different number of arguments in return statement than in returns declaration.
// TypeError 5547: (229-231): Empty tuple on the left hand side.
// TypeError 5547: (459-462): Empty tuple on the left hand side.
// TypeError 4289: (457-525): Compound assignment is not allowed for tuple types.
// TypeError 7407: (468-525): Type bytes32 is not implicitly convertible to expected type tuple().
// TypeError 9322: (447-454): No matching declaration found after argument-dependent lookup.
