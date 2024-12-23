{
	for {} 1 {}
	{
		let x:bool
		for { continue } x {} {}
	}
}
// ====
// dialect: zvmTyped
// ----
// SyntaxError 9615: (39-47): Keyword "continue" in for-loop init block is not allowed.
