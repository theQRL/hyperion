{
	for { pop(i) } 1 { } { let i := 1 }
}
// ====
// dialect: zvm
// ----
// DeclarationError 8198: (13-14): Identifier "i" not found.
