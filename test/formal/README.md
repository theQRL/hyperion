The Hyperion compiler implements several [optimization rules](https://github.com/theQRL/hyperion/blob/develop/libzvmasm/RuleList.h).

This directory contains an effort to formally prove the correctness of those rules in:

- HOL with [EthIsabelle](https://github.com/ekpyron/eth-isabelle)
- FOL with SMT solvers using [Integers and BitVectors](http://smtlib.cs.uiowa.edu/theories.shtml)
