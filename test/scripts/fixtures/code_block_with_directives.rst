A normal block with parameters.

.. code-block:: hyperion
    :force:
    :language: Hyperion

    // SPDX-License-Identifier: GPL-3.0
    pragma hyperion >=0.7.0 <0.9.0;

    contract C {
        function foo() public view {}
    }


.. warning::
    A Warning.

.. code-block:: hyperion

    uint constant x = 42;

Text.

::

    contract C {}

A block with blank lines between block parameters.
Sphinx will treat the second one as a part of the code.

.. code-block:: hyperion
    :force:

    :language: Hyperion

    contract D {}
    :linenos:

Block with parameters indented less than code.
Sphinx does not complain about these.

.. code-block:: hyperion
  :force:
  :linenos:

    contract E {}

More text.

.. code-block:: yul

    :force:
    let x := add(1, 5)

.. code-block:: yul

    :linenos:
    :language: Yul
    // Yul code wrapped in object
    {
        let y := mul(3, 5)
    }

.. code-block:: yul

    // Yul code wrapped in named object
    object "Test" {
        let y := mul(3, 5)
    :linenos:
    }

