library C {
    enum EnumType {A, B, C}

    struct StructType {
        uint x;
    }

    function f1(function (StructType memory) external f) external {}
    function f2(function (EnumType) external f) external {}
    function f3(function (EnumType, StructType memory) external f) external {}
}
// ----
