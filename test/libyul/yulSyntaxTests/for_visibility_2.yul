{
	for {} i { let i := 1 } {}
}
// ====
// dialect: zvm
// ----
// DeclarationError 8198: (10-11): Identifier "i" not found.
