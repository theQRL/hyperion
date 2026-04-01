{
	for {} 1 { let i := 1 } { pop(i) }
}
// ====
// dialect: qrvm
// ----
// DeclarationError 8198: (33-34): Identifier "i" not found.
