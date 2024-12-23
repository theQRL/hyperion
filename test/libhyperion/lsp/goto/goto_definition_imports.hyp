// SPDX-License-Identifier: UNLICENSED
pragma hyperion >=0.8.0;

import {Weather as Wetter} from "./lib.hyp";
//       ^ @wheatherImportCursor
import "./lib.hyp" as That;
//                    ^^^^ @ThatImport

contract C
{
    function test_symbol_alias() public pure returns (Wetter result)
                                             //        ^ @WetterCursor
    {
        result = Wetter.Sunny;
    }

    function test_library_alias() public pure returns (That.Color result)
                                                //     ^ @ThatCursor
    {
        That.Color color = That.Color.Red;
//      ^ @ThatVarCursor   ^ @ThatExpressionCursor
        result = color;
    }
}
// ----
// lib: @diagnostics 2072
// -> textDocument/definition {
//     "position": @wheatherImportCursor
// }
// <- [
//     {
//         "range": @whetherEnum,
//         "uri": "lib.hyp"
//     }
// ]
// -> textDocument/definition {
//     "position": @WetterCursor
// }
// <- [
//     {
//         "range": @whetherEnum,
//         "uri": "lib.hyp"
//     }
// ]
// -> textDocument/definition {
//     "position": @ThatCursor
// }
// <- [
//     {
//         "range": @ColorEnum,
//         "uri": "lib.hyp"
//     }
// ]
// -> textDocument/definition {
//     "position": @ThatVarCursor
// }
// <- [
//     {
//         "range": @ColorEnum,
//         "uri": "lib.hyp"
//     }
// ]
// -> textDocument/definition {
//     "position": @ThatExpressionCursor
// }
// <- [
//     {
//         "range": @ThatImport,
//         "uri": "goto_definition_imports.hyp"
//     }
// ]
