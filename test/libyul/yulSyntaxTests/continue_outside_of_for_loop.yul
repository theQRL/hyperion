{
	let x:bool
	if x { continue }
}
// ====
// dialect: zvmTyped
// ----
// SyntaxError 2592: (22-30): Keyword "continue" needs to be inside a for-loop body.
