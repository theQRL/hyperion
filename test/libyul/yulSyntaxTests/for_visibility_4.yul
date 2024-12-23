{
	for {} 1 { pop(i) } { let i := 1 }
}
// ====
// dialect: zvm
// ----
// DeclarationError 8198: (18-19): Identifier "i" not found.
