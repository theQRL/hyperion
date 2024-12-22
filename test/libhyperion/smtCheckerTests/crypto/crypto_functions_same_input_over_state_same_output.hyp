contract C {
	bytes data;
	bytes pubkey;
	bytes withdrawal_credentials;
	bytes amount;
	bytes signature;

	bytes32 kec;
	bytes32 sha;
	bytes32 dep;

	constructor(bytes memory _data, bytes memory _pubkey, bytes memory _withdrawal_credentials, bytes memory _amount, bytes memory _signature) {
		data = _data;
		pubkey = _pubkey;
		withdrawal_credentials = _withdrawal_credentials;
		amount = _amount;
		signature = _signature;

		kec = keccak256(data);
		sha = sha256(data);
		dep = depositroot(pubkey, withdrawal_credentials, amount, signature);
	}

	function f() public view {
		bytes32 _kec = keccak256(data);
		bytes32 _sha = sha256(data);
		bytes32 _dep = depositroot(pubkey, withdrawal_credentials, amount, signature);
		assert(_kec == kec);
		assert(_sha == sha);
		assert(_dep == dep);
	}
}
// ====
// SMTEngine: all
// ----
// Info 1391: CHC: 3 verification condition(s) proved safe! Enable the model checker option "show proved safe" to see all of them.
