contract C {
    function f() view public {
        payable(this).transfer(1);
    }
    function g() view public {
        require(payable(this).send(2));
    }
    function h() view public {
        (bool success,) = address(this).delegatecall("");
        require(success);
    }
    function i() view public {
        (bool success,) = address(this).call("");
        require(success);
    }
    receive() payable external {
    }
}
// ----
// TypeError 8961: (52-77): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (132-153): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (219-249): Function cannot be declared as view because this expression (potentially) modifies the state.
// TypeError 8961: (340-362): Function cannot be declared as view because this expression (potentially) modifies the state.
