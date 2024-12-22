contract C {
	bytes32 bhash;
	address coin;
	uint prevrandao;
	uint glimit;
	uint number;
	uint tstamp;
	bytes mdata;
	address sender;
	bytes4 sig;
	uint value;
	uint gprice;
	address origin;

	function f() public payable {
		bhash = blockhash(12);
		coin = block.coinbase;
		prevrandao = block.prevrandao;
		glimit = block.gaslimit;
		number = block.number;
		tstamp = block.timestamp;
		mdata = msg.data;
		sender = msg.sender;
		sig = msg.sig;
		value = msg.value;
		gprice = tx.gasprice;
		origin = tx.origin;

		fi();

		assert(bhash == blockhash(122));
		assert(coin != block.coinbase);
		assert(prevrandao != block.prevrandao);
		assert(glimit != block.gaslimit);
		assert(number != block.number);
		assert(tstamp != block.timestamp);
		assert(mdata.length != msg.data.length);
		assert(sender != msg.sender);
		assert(sig != msg.sig);
		assert(value != msg.value);
		assert(gprice != tx.gasprice);
		assert(origin != tx.origin);
	}

	function fi() internal view {
		assert(bhash == blockhash(122));
		assert(coin != block.coinbase);
		assert(prevrandao != block.prevrandao);
		assert(glimit != block.gaslimit);
		assert(number != block.number);
		assert(tstamp != block.timestamp);
		assert(mdata.length != msg.data.length);
		assert(sender != msg.sender);
		assert(sig != msg.sig);
		assert(value != msg.value);
		assert(gprice != tx.gasprice);
		assert(origin != tx.origin);
	}
}
// ====
// SMTEngine: all
// SMTIgnoreCex: yes
// ----
// Warning 6328: (526-557): CHC: Assertion violation happens here.
// Warning 6328: (561-591): CHC: Assertion violation happens here.
// Warning 6328: (595-633): CHC: Assertion violation happens here.
// Warning 6328: (637-669): CHC: Assertion violation happens here.
// Warning 6328: (673-703): CHC: Assertion violation happens here.
// Warning 6328: (707-740): CHC: Assertion violation happens here.
// Warning 6328: (744-783): CHC: Assertion violation happens here.
// Warning 6328: (787-815): CHC: Assertion violation happens here.
// Warning 6328: (819-841): CHC: Assertion violation happens here.
// Warning 6328: (845-871): CHC: Assertion violation happens here.
// Warning 6328: (875-904): CHC: Assertion violation happens here.
// Warning 6328: (908-935): CHC: Assertion violation happens here.
// Warning 6328: (974-1005): CHC: Assertion violation happens here.
// Warning 6328: (1009-1039): CHC: Assertion violation happens here.
// Warning 6328: (1043-1081): CHC: Assertion violation happens here.
// Warning 6328: (1085-1117): CHC: Assertion violation happens here.
// Warning 6328: (1121-1151): CHC: Assertion violation happens here.
// Warning 6328: (1155-1188): CHC: Assertion violation happens here.
// Warning 6328: (1192-1231): CHC: Assertion violation happens here.
// Warning 6328: (1235-1263): CHC: Assertion violation happens here.
// Warning 6328: (1267-1289): CHC: Assertion violation happens here.
// Warning 6328: (1293-1319): CHC: Assertion violation happens here.
// Warning 6328: (1323-1352): CHC: Assertion violation happens here.
// Warning 6328: (1356-1383): CHC: Assertion violation happens here.
