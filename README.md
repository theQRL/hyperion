# The Hyperion Contract-Oriented Programming Language

Hyperion is a statically typed, contract-oriented, high-level language for implementing smart contracts on the Zond platform.

For a good overview and starting point, please check out the official [Hyperion Language Portal](https://soliditylang.org).

## Table of Contents

- [Background](#background)
- [Build and Install](#build-and-install)
- [Example](#example)
- [Documentation](#documentation)
- [Development](#development)
- [Maintainers](#maintainers)
- [License](#license)
- [Security](#security)

## Background

Hyperion is a statically-typed curly-braces programming language designed for developing smart contracts
that run on the Zond Virtual Machine. Smart contracts are programs that are executed inside a peer-to-peer
network where nobody has special authority over the execution, and thus they allow anyone to implement tokens of value,
ownership, voting, and other kinds of logic.

When deploying contracts, you should use the latest released version of
Hyperion. This is because breaking changes, as well as new features and bug fixes, are
introduced regularly. We currently use a 0.x version
number [to indicate this fast pace of change](https://semver.org/#spec-item-4).

## Build and Install

Instructions about how to build and install the Hyperion compiler can be
found in the [Hyperion documentation](https://docs.soliditylang.org/en/latest/installing-hyperion.html#building-from-source).


## Example

A "Hello World" program in Hyperion is of even less use than in other languages, but still:

```hyperion
// SPDX-License-Identifier: MIT
pragma hyperion >=0.6.0 <0.9.0;

contract HelloWorld {
    function helloWorld() external pure returns (string memory) {
        return "Hello, World!";
    }
}
```

To get started with Hyperion, you can use [Remix](https://remix.ethereum.org/), which is a
browser-based IDE. Here are some example contracts:

1. [Voting](https://docs.soliditylang.org/en/latest/solidity-by-example.html#voting)
2. [Blind Auction](https://docs.soliditylang.org/en/latest/solidity-by-example.html#blind-auction)
3. [Safe remote purchase](https://docs.soliditylang.org/en/latest/solidity-by-example.html#safe-remote-purchase)

## Documentation

The Hyperion documentation is hosted using [Read the Docs](https://docs.soliditylang.org).

## Development

Hyperion is still under development. Contributions are always welcome!
Please follow the
[Developers Guide](https://docs.soliditylang.org/en/latest/contributing.html)
if you want to help.

You can find our current feature and bug priorities for forthcoming
releases in the [projects section](https://github.com/theQRL/hyperion/projects).

## Maintainers
The Hyperion programming language and compiler are open-source community projects governed by a core team.
The core team is sponsored by the [Ethereum Foundation](https://ethereum.foundation/).

## License
Hyperion is licensed under [GNU General Public License v3.0](LICENSE.txt).

Some third-party code has its [own licensing terms](cmake/templates/license.h.in).

## Security

The security policy may be [found here](SECURITY.md).
