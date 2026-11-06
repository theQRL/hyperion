.. index:: ! denomination

**************************************
Units and Globally Available Variables
**************************************

.. index:: ! planck, ! shor, ! quanta, ! denomination;qrl

QRL Units
===========

A literal number can take a suffix of ``planck``, ``shor`` or ``quanta`` to specify a subdenomination of QRL, where QRL numbers without a postfix are assumed to be Planck.

.. code-block:: hyperion
    :force:

    assert(1 planck == 1);
    assert(1 shor == 1e9);
    assert(1 quanta == 1e18);

The only effect of the subdenomination suffix is a multiplication by a power of ten.

.. index:: ! seconds, ! minutes, ! hours, ! days, ! weeks, ! denomination;time

Time Units
==========

Suffixes like ``seconds``, ``minutes``, ``hours``, ``days`` and ``weeks``
after literal numbers can be used to specify units of time where seconds are the base
unit and units are considered naively in the following way:

* ``1 == 1 seconds``
* ``1 minutes == 60 seconds``
* ``1 hours == 60 minutes``
* ``1 days == 24 hours``
* ``1 weeks == 7 days``

Take care if you perform calendar calculations using these units, because
not every year equals 365 days and not even every day has 24 hours
because of `leap seconds <https://en.wikipedia.org/wiki/Leap_second>`_.
Due to the fact that leap seconds cannot be predicted, an exact calendar
library has to be updated by an external oracle.

These suffixes cannot be applied to variables. For example, if you want to
interpret a function parameter in days, you can in the following way:

.. code-block:: hyperion

    function f(uint start, uint daysAfter) public {
        if (block.timestamp >= start + daysAfter * 1 days) {
            // ...
        }
    }

.. _special-variables-functions:

Special Variables and Functions
===============================

There are special variables and functions which always exist in the global
namespace and are mainly used to provide information about the blockchain
or are general-use utility functions.

.. index:: abi, block, coinbase, prevrandao, encode, number, block;number, timestamp, block;timestamp, msg, data, gas, sender, value, gas price, origin


Block and Transaction Properties
--------------------------------

- ``blockhash(uint256 blockNumber) returns (bytes32)``: hash of the given block when ``blocknumber`` is one of the 256 most recent blocks; otherwise returns zero
- ``block.basefee`` (``uint256``): current block's base fee
- ``block.chainid`` (``uint256``): current chain id
- ``block.coinbase`` (``address payable``): current block miner's address
- ``block.gaslimit`` (``uint256``): current block gaslimit
- ``block.number`` (``uint256``): current block number
- ``block.prevrandao`` (``uint256``): random number provided by the beacon chain
- ``block.timestamp`` (``uint256``): current block timestamp as seconds since unix epoch
- ``gasleft() returns (uint256)``: remaining gas
- ``msg.data`` (``bytes calldata``): complete calldata
- ``msg.sender`` (``address``): sender of the message (current call)
- ``msg.sig`` (``bytes4``): first four bytes of the calldata (i.e. function identifier)
- ``msg.value`` (``uint``): number of planck sent with the message
- ``tx.gasprice`` (``uint256``): gas price of the transaction
- ``tx.origin`` (``address``): sender of the transaction (full call chain)

.. note::
    The values of all members of ``msg``, including ``msg.sender`` and
    ``msg.value`` can change for every **external** function call.
    This includes calls to library functions.

.. note::
    When contracts are evaluated off-chain rather than in context of a transaction included in a
    block, you should not assume that ``block.*`` and ``tx.*`` refer to values from any specific
    block or transaction. These values are provided by the QRVM implementation that executes the
    contract and can be arbitrary.

.. note::
    Do not rely on ``block.timestamp`` or ``blockhash`` as a source of randomness,
    unless you know what you are doing.

    Both the timestamp and the block hash can be influenced by miners to some degree.
    Bad actors in the mining community can for example run a casino payout function on a chosen hash
    and just retry a different hash if they did not receive any compensation, e.g. Quanta.

    The current block timestamp must be strictly larger than the timestamp of the last block,
    but the only guarantee is that it will be somewhere between the timestamps of two
    consecutive blocks in the canonical chain.

.. note::
    The block hashes are not available for all blocks for scalability reasons.
    You can only access the hashes of the most recent 256 blocks, all other
    values will be zero.

.. index:: abi, encoding, packed

ABI Encoding and Decoding Functions
-----------------------------------

- ``abi.decode(bytes memory encodedData, (...)) returns (...)``: ABI-decodes the given data, while the types are given in parentheses as second argument. Example: ``(uint a, uint[2] memory b, bytes memory c) = abi.decode(data, (uint, uint[2], bytes))``
- ``abi.encode(...) returns (bytes memory)``: ABI-encodes the given arguments
- ``abi.encodePacked(...) returns (bytes memory)``: Performs :ref:`packed encoding <abi_packed_mode>` of the given arguments. Note that packed encoding can be ambiguous!
- ``abi.encodeWithSelector(bytes4 selector, ...) returns (bytes memory)``: ABI-encodes the given arguments starting from the second and prepends the given four-byte selector
- ``abi.encodeWithSignature(string memory signature, ...) returns (bytes memory)``: Equivalent to ``abi.encodeWithSelector(bytes4(keccak256(bytes(signature))), ...)``
- ``abi.encodeCall(function functionPointer, (...)) returns (bytes memory)``: ABI-encodes a call to ``functionPointer`` with the arguments found in the tuple. Performs a full type-check, ensuring the types match the function signature. Result equals ``abi.encodeWithSelector(functionPointer.selector, (...))``

.. note::
    These encoding functions can be used to craft data for external function calls without actually
    calling an external function. Furthermore, ``keccak256(abi.encodePacked(a, b))`` is a way
    to compute the hash of structured data (although be aware that it is possible to
    craft a "hash collision" using different function parameter types).

See the documentation about the :ref:`ABI <ABI>` and the
:ref:`tightly packed encoding <abi_packed_mode>` for details about the encoding.

.. index:: bytes members

Members of bytes
----------------

- ``bytes.concat(...) returns (bytes memory)``: :ref:`Concatenates variable number of bytes and bytes1, ..., bytes32 arguments to one byte array<bytes-concat>`

.. index:: string members

Members of string
-----------------

- ``string.concat(...) returns (string memory)``: :ref:`Concatenates variable number of string arguments to one string array<string-concat>`


.. index:: assert, revert, require

Error Handling
--------------

See the dedicated section on :ref:`assert and require<assert-and-require>` for
more details on error handling and when to use which function.

``assert(bool condition)``
    causes a Panic error and thus state change reversion if the condition is not met - to be used for internal errors.

``require(bool condition)``
    reverts if the condition is not met - to be used for errors in inputs or external components.

``require(bool condition, string memory message)``
    reverts if the condition is not met - to be used for errors in inputs or external components. Also provides an error message.

``revert()``
    abort execution and revert state changes

``revert(string memory reason)``
    abort execution and revert state changes, providing an explanatory string

.. index:: keccak256, sha256, shake256, mldsa87verify, depositroot, xmssverify, addmod, mulmod, cryptography,

.. _mathematical-and-cryptographic-functions:

Mathematical and Cryptographic Functions
----------------------------------------

``addmod(uint x, uint y, uint k) returns (uint)``
    compute ``(x + y) % k`` where the addition is performed with arbitrary precision and does not wrap around at ``2**512``. Revert with ``Panic(0x12)`` if ``k == 0``.

``mulmod(uint x, uint y, uint k) returns (uint)``
    compute ``(x * y) % k`` where the multiplication is performed with arbitrary precision and does not wrap around at ``2**512``. Revert with ``Panic(0x12)`` if ``k == 0``.

Here ``uint`` is an alias for ``uint512``, the full QRVM word. Narrower
unsigned arguments such as ``uint256`` widen implicitly. Assigning the result to
a narrower type requires an explicit conversion. If the nonzero modulus fits in
that narrower unsigned type, the result fits too, because it is less than the modulus.
Earlier releases used ``uint256`` parameters and return values.

``keccak256(bytes memory) returns (bytes32)``
    compute the Keccak-256 hash of the input

``sha256(bytes memory) returns (bytes32)``
    compute the SHA-256 hash of the input

``shake256(bytes memory) returns (bytes64)``
    compute a fixed 512-bit SHAKE256 digest of the input through precompile address ``0x06``

``mldsa87verify(bytes64 digest, bytes memory signature, bytes memory publicKey, bytes memory context) returns (bool)``
    verify a FIPS 204 ML-DSA-87 signature over the 64-byte message representative ``digest``; implemented by the precompiled contract at address ``3``. ``digest`` has to be a ``bytes64`` value, such as the result of ``shake256``: a narrower fixed-size bytes value or a shorter literal is rejected, because the implicit conversion would pad it with zero bytes on the right. Use an explicit ``bytes64(...)`` conversion if that padding is intended. The compiler sends ``digest || publicKey || signature || uint8(context.length) || context``. The signature must be 4627 bytes, the public key 2592 bytes and the context at most 255 bytes. Arguments of other lengths yield ``false`` without calling the precompiled contract. Only exactly 64 bytes of return data holding the value ``1`` map to ``true``. Any other return data maps to ``false``, including the empty data the precompile returns for an invalid signature. If the precompile call fails, for example because it runs out of gas, the call reverts.

``depositroot(bytes memory pubkey, bytes memory withdrawal_recipient, bytes memory amount, bytes memory randao_commitment, bytes memory sig) returns (bytes32)``
    compute the SSZ hash tree root of the beacon chain ``DepositData`` container (2592-byte ML-DSA-87 public key, 64-byte withdrawal recipient, 8-byte little-endian amount, 32-byte RANDAO commitment, 4627-byte signature); implemented by the precompiled contract at address ``1``. The compiler does not check the lengths: the precompiled contract reads the fields at fixed offsets of their concatenation, so a field of the wrong length shifts the fields after it, missing bytes read as zero and extra bytes are ignored. Check the lengths first if they come from untrusted input.

``xmssverify(bytes memory message, bytes memory signature, bytes memory extendedPublicKey) returns (bool)``
    verify an XMSS signature produced by a legacy QRL wallet, with the semantics of ``XmssBase::verify`` of `qrllib <https://github.com/theQRL/qrllib>`_: ``extendedPublicKey`` is the 67-byte qrllib extended public key (3-byte QRL descriptor, 32-byte root, 32-byte public seed), ``signature`` the ``2180 + 32 * height`` byte XMSS signature (4-byte index, 32-byte randomness, 67 x 32-byte WOTS+ signature with ``w = 16``, ``height`` x 32-byte authentication path) and ``message`` the signed data, of arbitrary length. SHA2-256, SHAKE-128 and SHAKE-256 keys with tree heights 4 to 30 are supported. Returns ``false`` (instead of reverting) for malformed keys or signatures; implemented by the precompiled contract at address ``7``

.. note::

    The ``xmssverify`` precompiled contract at address ``7`` receives ``uint32 message_length || message || signature || extendedPublicKey`` (a 4-byte big-endian message length followed by the three byte strings without padding) and returns one 64-byte word holding ``1`` if the signature is valid and no data otherwise. As for ``mldsa87verify``, only exactly 64 bytes of return data holding the value ``1`` map to ``true``. The precompiled contract takes the trailing 67 bytes of its input as the key, so the compiler only calls it when ``extendedPublicKey`` is exactly 67 bytes long and yields ``false`` for any other key length. A QRL address is ``descriptor || sha256(extendedPublicKey) || checksum``, so a contract can bind a key to a known ``Q`` address by comparing ``sha256(extendedPublicKey)`` with bytes 3 to 34 of that address. Note that XMSS is a stateful one-time-signature scheme: every signature reveals a tree leaf, so a contract should bind the signed message to its own context (e.g. the caller and a nonce) to prevent replays.

.. note::

    When running ``sha256``, ``shake256``, ``mldsa87verify``, ``depositroot`` or ``xmssverify`` on a *private blockchain*, you might encounter Out-of-Gas. This is because these functions are implemented as "precompiled contracts" and only really exist after they receive the first message (although their contract code is hardcoded). Messages to non-existing contracts are more expensive and thus the execution might run into an Out-of-Gas error. A workaround for this problem is to first send Planck (1 for example) to each of the contracts before you use them in your actual contracts. This is not an issue on the main or test net.

.. index:: balance, codehash, send, transfer, call, delegatecall, staticcall

.. _address_related:

Members of Address Types
------------------------

``<address>.balance`` (``uint``)
    balance of the :ref:`address` in Planck

``<address>.code`` (``bytes memory``)
    code at the :ref:`address` (can be empty)

``<address>.codehash`` (``bytes32``)
    the codehash of the :ref:`address`

``<address payable>.transfer(uint amount)``
    send given amount of Planck to :ref:`address`, reverts on failure, forwards 2300 gas stipend, not adjustable

``<address payable>.send(uint amount) returns (bool)``
    send given amount of Planck to :ref:`address`, returns ``false`` on failure, forwards 2300 gas stipend, not adjustable

``<address>.call(bytes memory) returns (bool, bytes memory)``
    issue low-level ``CALL`` with the given payload, returns success condition and return data, forwards all available gas, adjustable

``<address>.delegatecall(bytes memory) returns (bool, bytes memory)``
    issue low-level ``DELEGATECALL`` with the given payload, returns success condition and return data, forwards all available gas, adjustable

``<address>.staticcall(bytes memory) returns (bool, bytes memory)``
    issue low-level ``STATICCALL`` with the given payload, returns success condition and return data, forwards all available gas, adjustable

For more information, see the section on :ref:`address`.

.. warning::
    You should avoid using ``.call()`` whenever possible when executing another contract function as it bypasses type checking,
    function existence check, and argument packing.

.. warning::
    There are some dangers in using ``send``: The transfer fails if the call stack depth is at 1024
    (this can always be forced by the caller) and it also fails if the recipient runs out of gas. So in order
    to make safe Quanta transfers, always check the return value of ``send``, use ``transfer`` or even better:
    Use a pattern where the recipient withdraws the Quanta.

.. warning::
    Due to the fact that the QRVM considers a call to a non-existing contract to always succeed,
    Hyperion includes an extra check using the ``extcodesize`` opcode when performing external calls.
    This ensures that the contract that is about to be called either actually exists (it contains code)
    or an exception is raised.

    The low-level calls which operate on addresses rather than contract instances (i.e. ``.call()``,
    ``.delegatecall()``, ``.staticcall()``, ``.send()`` and ``.transfer()``) **do not** include this
    check, which makes them cheaper in terms of gas but also less safe.

.. note::
   Prior to version 0.5.0, Hyperion allowed address members to be accessed by a contract instance, for example ``this.balance``.
   This is now forbidden and an explicit conversion to address must be done: ``address(this).balance``.

.. note::
   If state variables are accessed via a low-level delegatecall, the storage layout of the two contracts
   must align in order for the called contract to correctly access the storage variables of the calling contract by name.
   This is of course not the case if storage pointers are passed as function arguments as in the case for
   the high-level libraries.


.. index:: this, super

Contract-related
----------------

``this`` (current contract's type)
    The current contract, explicitly convertible to :ref:`address`

``super``
    A contract one level higher in the inheritance hierarchy

Furthermore, all functions of the current contract are callable directly including the current function.

.. index:: type, creationCode, runtimeCode

.. _meta-type:

Type Information
----------------

The expression ``type(X)`` can be used to retrieve information about the type
``X``. Currently, there is limited support for this feature (``X`` can be either
a contract or an integer type) but it might be expanded in the future.

The following properties are available for a contract type ``C``:

``type(C).name``
    The name of the contract.

``type(C).creationCode``
    Memory byte array that contains the creation bytecode of the contract.
    This can be used in inline assembly to build custom creation routines,
    especially by using the ``create2`` opcode.
    This property can **not** be accessed in the contract itself or any
    derived contract. It causes the bytecode to be included in the bytecode
    of the call site and thus circular references like that are not possible.

``type(C).runtimeCode``
    Memory byte array that contains the runtime bytecode of the contract.
    This is the code that is usually deployed by the constructor of ``C``.
    If ``C`` has a constructor that uses inline assembly, this might be
    different from the actually deployed bytecode. Also note that libraries
    modify their runtime bytecode at time of deployment to guard against
    regular calls.
    The same restrictions as with ``.creationCode`` also apply for this
    property.

In addition to the properties above, the following properties are available
for an interface type ``I``:

``type(I).interfaceId``
    A ``bytes4`` value containing the `EIP-165 <https://eips.ethereum.org/EIPS/eip-165>`_
    interface identifier of the given interface ``I``. This identifier is defined as the ``XOR`` of all
    function selectors defined within the interface itself - excluding all inherited functions.

The following properties are available for an integer type ``T``:

``type(T).min``
    The smallest value representable by type ``T``.

``type(T).max``
    The largest value representable by type ``T``.

Reserved Keywords
=================

These keywords are reserved in Hyperion. They might become part of the syntax in the future:

``after``, ``alias``, ``apply``, ``auto``, ``byte``, ``case``, ``copyof``, ``default``,
``define``, ``final``, ``implements``, ``in``, ``inline``, ``let``, ``macro``, ``match``,
``mutable``, ``null``, ``of``, ``partial``, ``promise``, ``reference``, ``relocatable``,
``sealed``, ``sizeof``, ``static``, ``supports``, ``switch``, ``typedef``, ``typeof``,
``var``.
