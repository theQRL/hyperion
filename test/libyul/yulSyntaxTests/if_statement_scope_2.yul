{
	if 32 { let x := 3 }
	x := 2
}
// ====
// dialect: zvm
// ----
// DeclarationError 4634: (25-26): Variable not found or variable not lvalue.
