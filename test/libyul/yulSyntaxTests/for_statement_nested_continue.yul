{
	for {let i := 0} iszero(eq(i, 10)) {}
	{
		function f() { continue }
	}
}
// ====
// dialect: qrvmTyped 
// ----
// SyntaxError 2592: (61-69): Keyword "continue" needs to be inside a for-loop body.
