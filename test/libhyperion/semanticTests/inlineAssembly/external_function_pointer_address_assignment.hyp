contract C {
	function testFunction() external {}

	function testYul(address newAddress) view public returns (address adr) {
		function() external fp = this.testFunction;

		assembly {
			fp.address := newAddress
		}

		return fp.address;
	}
}
// ----
// testYul(address): Z1234567890 -> Z1234567890
// testYul(address): ZC0FFEE3EA7 -> ZC0FFEE3EA7
