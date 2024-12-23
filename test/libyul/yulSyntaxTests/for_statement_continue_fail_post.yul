{
	let x:bool
	for {let i := 0} x {i := add(i, 1) continue} {}
}
// ====
// dialect: zvmTyped
// ----
// SyntaxError 2461: (50-58): Keyword "continue" in for-loop post block is not allowed.
