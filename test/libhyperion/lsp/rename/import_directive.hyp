// SPDX-License-Identifier: UNLICENSED
pragma hyperion >=0.8.0;

import "./contract.hyp" as externalFile;
//                         ^^^^^^^^^^^^ @FileAliasInImportDirective
//                             ^ @CursorOnFileAliasInImportDirective
import {ToRename as ExternalContract, User} from "./contract.hyp";
//                  ^^^^^^^^^^^^^^^^ @RenamedContractInImportDirective
//                         ^ @CursorOnRenamedContractInImportDirective
//      ^^^^^^^^ @OriginalNameInImportDirective
//      ^ @CursorOnOriginalNameInImportDirective
//                                    ^^^^ @UserInImportDirective
//                                    ^ @CursorOnUserInImportDirective

contract C
{
    ExternalContract public externalContract;
//  ^^^^^^^^^^^^^^^^ @RenamedContractInPublicVariable
//        ^ @CursorOnRenamedContractInPublicVariable
    externalFile.ToRename public externalFileContract;
//  ^^^^^^^^^^^^ @FileAliasInPublicVariable
//  ^ @CursorOnFileAliasInPublicVariable
//               ^^^^^^^^ @OriginalNameInPublicVariable
//                      ^ @CursorOnOriginalNameInPublicVariable
    User public externalUserContract;
//  ^^^^ @UserInPublicVariable
//     ^ @CursorOnUserInPublicVariable
}
// ----
// contract:
// -> textDocument/rename {
//     "newName": "Renamed",
//     "position": @CursorOnFileAliasInImportDirective
// }
// <- {
//     "changes": {
//         "rename/import_directive.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @FileAliasInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @FileAliasInImportDirective
//             }
//         ]
//     }
// }
// -> textDocument/rename {
//     "newName": "Renamed",
//     "position": @CursorOnRenamedContractInImportDirective
// }
// <- {
//     "changes": {
//         "rename/import_directive.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @RenamedContractInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @RenamedContractInImportDirective
//             }
//         ]
//     }
// }
// -> textDocument/rename {
//     "newName": "Renamed",
//     "position": @CursorOnOriginalNameInImportDirective
// }
// <- {
//     "changes": {
//         "rename/contract.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInParameter
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInReturnExpression
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInReturnParameter
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInMapping
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInArrayType
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInDefinition
//             }
//         ],
//         "rename/import_directive.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @OriginalNameInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @OriginalNameInImportDirective
//             }
//         ]
//     }
// }
// -> textDocument/rename {
//     "newName": "Renamed",
//     "position": @CursorOnRenamedContractInPublicVariable
// }
// <- {
//     "changes": {
//         "rename/import_directive.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @RenamedContractInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @RenamedContractInImportDirective
//             }
//         ]
//     }
// }
// -> textDocument/rename {
//     "newName": "Renamed",
//     "position": @CursorOnFileAliasInPublicVariable
// }
// <- {
//     "changes": {
//         "rename/import_directive.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @FileAliasInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @FileAliasInImportDirective
//             }
//         ]
//     }
// }
// -> textDocument/rename {
//     "newName": "Renamed",
//     "position": @CursorOnOriginalNameInPublicVariable
// }
// <- {
//     "changes": {
//         "rename/contract.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInParameter
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInReturnExpression
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInReturnParameter
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInMapping
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInArrayType
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @ContractInDefinition
//             }
//         ],
//         "rename/import_directive.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @OriginalNameInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @OriginalNameInImportDirective
//             }
//         ]
//     }
// }
// -> textDocument/rename {
//     "newName": "Renamed",
//     "position": @CursorOnUserInPublicVariable
// }
// <- {
//     "changes": {
//         "rename/contract.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @UserContractInContractTest
//             }
//         ],
//         "rename/import_directive.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @UserInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @UserInImportDirective
//             }
//         ]
//     }
// }
// -> textDocument/rename {
//     "newName": "Renamed",
//     "position": @CursorOnUserInImportDirective
// }
// <- {
//     "changes": {
//         "rename/contract.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @UserContractInContractTest
//             }
//         ],
//         "rename/import_directive.hyp": [
//             {
//                 "newText": "Renamed",
//                 "range": @UserInPublicVariable
//             },
//             {
//                 "newText": "Renamed",
//                 "range": @UserInImportDirective
//             }
//         ]
//     }
// }
